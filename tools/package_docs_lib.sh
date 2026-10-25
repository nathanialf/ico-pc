# shellcheck shell=bash
# tools/package_docs_lib.sh: the player guides of the release packages
# (docs/*.md that README.md links) and the package's ico-pc.ini, sourced by
# package_win.sh and package_linux.sh. Functions only. BUILDING.md, LEGAL.md
# and THIRD_PARTY.md are developer documents and are not in this list (the
# Linux package ships THIRD_PARTY.md at its root).

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

# pkg_write_ini <file> <windows|linux> <iso> [portable]: the package's
# ico-pc.ini, its notes in the platform's wording, with the user's iso= value
# kept. With a 4th argument "portable" the portable archive's file: the
# line portable=1 is on (the game makes userdata beside the program).
pkg_write_ini() {
    local portable_block mp_script=make_portable.sh
    [[ "$2" == windows ]] && mp_script=make_portable.cmd
    if [[ "${4:-}" == portable ]]; then
        portable_block='# portable: this copy keeps your saves, settings and the game'"'"'s data in a
# folder named userdata next to the program (made at the first start),
# instead of in your user folder. portable=0 turns it off, and the folder
# is then not used.
portable=1'
    else
        portable_block="# portable: remove the # in front of the next line to keep your saves,
# settings and the game's data in a folder named userdata next to the
# program, instead of in your user folder. Making an empty userdata folder
# next to the program does the same, and portable=0 turns it off even when
# that folder exists. The script $mp_script next to the program makes the
# folder and offers to move your existing files into it.
# portable=1"
    fi
    if [[ "$2" == windows ]]; then
        cat > "$1" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of ICO, the European disc
# (SCES-50760), the Japanese disc (SCPS-11003), the Korean disc
# (SCPS-56001), the North American disc (SCUS-97113), the Asian disc
# (SCPS-55001), the August 2001 preview disc or the June 2001 magazine demo
# (SCUS-97158): a .iso, a .chd, or a
# .bin with its .cue, for example iso=C:\\Games\\Ico_PAL.iso . A file named
# Ico_PAL.iso, Ico_PAL.chd, Ico_PAL.bin or Ico_PAL.cue (European disc), or
# the same with Ico_JPN (Japanese disc), Ico_KOR (Korean disc), Ico_USA (North
# American disc), Ico_CN (Asian disc), Ico_AUG6 (August 2001 preview) or
# Ico_JUN26 (June 2001 magazine demo), next to
# the program is used before this line. Leave it empty and the first start asks for the image, then
# saves your choice here. The image is read once, to copy the game's data
# into your user folder (%APPDATA%\\ico-pc\\ico-pc); later starts do not
# need it.
iso=$3

# watchdog: if the game has not started this many seconds after launch
# (twice as many while ReShade is loaded), or stops responding for twice as
# long later, the program writes what it was doing to logs\\ico-pc.log and
# closes. 0 turns it off.
watchdog=30

$portable_block

# texture packs: see the README.txt in textures\\SCES-50760\\replacements
# (European disc), textures\\SCPS-11003\\replacements (Japanese disc),
# textures\\SCPS-56001\\replacements (Korean disc),
# textures\\SCUS-97113\\replacements (North American disc and August 2001
# preview), textures\\SCUS-97158\\replacements (June 2001 magazine demo) or
# textures\\SCPS-55001\\replacements (Asian disc) next to the program.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, say which disc you play (the European,
# Japanese, Korean, North American or Asian disc, the August 2001 preview or
# the June 2001 magazine demo) and send the newest logs\\report-<date>.zip from next to
# the program (or logs\\ico-pc.log).
INI
    else
        cat > "$1" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of ICO, the European disc
# (SCES-50760), the Japanese disc (SCPS-11003), the Korean disc
# (SCPS-56001), the North American disc (SCUS-97113), the Asian disc
# (SCPS-55001), the August 2001 preview disc or the June 2001 magazine demo
# (SCUS-97158): a .iso, a .chd, or a
# .bin with its .cue, for example iso=/home/deck/Games/Ico_PAL.iso . A file
# named Ico_PAL.iso, Ico_PAL.chd, Ico_PAL.bin or Ico_PAL.cue (European
# disc), or the same with Ico_JPN (Japanese disc), Ico_KOR (Korean disc),
# Ico_USA (North American disc), Ico_CN (Asian disc), Ico_AUG6 (August 2001
# preview) or Ico_JUN26 (June 2001 magazine demo),
# next to ico_pc is used before this line.
# Leave it empty and the first start asks for the image (the file window
# comes from your desktop, or from the small program zenity), then saves
# your choice here. The image is read once, to
# copy the game's data into your user folder (~/.local/share/ico-pc/ico-pc/);
# later starts do not need it.
iso=$3

# watchdog: if the game has not started this many seconds after launch
# (twice as many while vkBasalt is loaded), or stops responding for twice as
# long later, the program writes what it was doing to logs/ico-pc.log and
# closes. 0 turns it off.
watchdog=30

$portable_block

# texture packs: see the README.txt in textures/SCES-50760/replacements
# (European disc), textures/SCPS-11003/replacements (Japanese disc),
# textures/SCPS-56001/replacements (Korean disc),
# textures/SCUS-97113/replacements (North American disc and August 2001
# preview), textures/SCUS-97158/replacements (June 2001 magazine demo) or
# textures/SCPS-55001/replacements (Asian disc) next to ico_pc.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, say which disc you play (the European,
# Japanese, Korean, North American or Asian disc, the August 2001 preview or
# the June 2001 magazine demo) and send the newest logs/report-<date>.zip from next to
# ico_pc (or logs/ico-pc.log).
INI
    fi
}

# pkg_portable_readme_note <file>: appends the portable archive's paragraph
# to the staged README.md
pkg_portable_readme_note() {
    cat >> "$1" <<'MD'

## This is the portable download

This copy keeps your saves, settings and the game's data in a folder named
`userdata` beside the program (inside `x64` on Windows), so it can live on a
USB stick or sit next to another copy. The folder is made at the first
start. To use your user folder instead, put `portable=0` in `ico-pc.ini`.
See [Saves and portable mode](docs/PORTABLE_MODE.md).
MD
}
