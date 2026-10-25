# shellcheck shell=bash
# tools/package_textures_lib.sh: the folder notes of the release packages
# (textures, models, reshade), the zip writer and the archive checks,
# sourced by package_win.sh and package_linux.sh and by
# tools/test/package_textures_test.sh. Functions only.

# the serial folders: one per pack folder the game reads, the European
# (SCES-50760), the Japanese (SCPS-11003), the Korean (SCPS-56001), the North
# American (SCUS-97113, which the August 2001 preview shares: its region row's
# pack_serial), the June 2001 magazine demo (SCUS-97158) and the Asian
# (SCPS-55001); the game reads the one of the disc it plays
pkg_serials=(SCES-50760 SCPS-11003 SCPS-56001 SCUS-97113 SCUS-97158 SCPS-55001)

# pkg_disc_name <serial>: the disc's name in the notes
pkg_disc_name() {
    case "$1" in
        SCPS-11003) echo "the Japanese disc" ;;
        SCPS-56001) echo "the Korean disc" ;;
        SCUS-97113) echo "the North American disc or the August 2001 preview disc" ;;
        SCUS-97158) echo "the June 2001 magazine demo" ;;
        SCPS-55001) echo "the Asian disc" ;;
        *) echo "the European disc" ;;
    esac
}

# pkg_other_discs <serial>: the other discs' names for the notes, "the
# Japanese disc (SCPS-11003), the Korean disc (SCPS-56001), ... or the Asian disc (SCPS-55001)"
pkg_other_discs() {
    local s out="" n=0 total=$(( ${#pkg_serials[@]} - 1 ))
    for s in "${pkg_serials[@]}"; do
        [[ "$s" == "$1" ]] && continue
        n=$((n + 1))
        if (( n > 1 )); then
            if (( n == total )); then out+=" or "; else out+=", "; fi
        fi
        out+="$(pkg_disc_name "$s") ($s)"
    done
    echo "$out"
}

# pkg_other_dirs <kind> <serial>: "kind/S1/replacements/ or kind/S2/replacements/"
pkg_other_dirs() {
    local s out="" n=0 total=$(( ${#pkg_serials[@]} - 1 ))
    for s in "${pkg_serials[@]}"; do
        [[ "$s" == "$2" ]] && continue
        n=$((n + 1))
        if (( n > 1 )); then
            if (( n == total )); then out+=" or "; else out+=", "; fi
        fi
        out+="$1/$s/replacements/"
    done
    echo "$out"
}

# the folder under the program folder the game looks in, and the note in it
# (the European disc's; the other discs' are the same with their serials)
pkg_textures_rel="textures/SCES-50760/replacements"
pkg_textures_rel_jpn="textures/SCPS-11003/replacements"
pkg_textures_rel_kor="textures/SCPS-56001/replacements"
pkg_textures_rel_usa="textures/SCUS-97113/replacements"
pkg_textures_rel_jun26="textures/SCUS-97158/replacements"
pkg_textures_rel_cn="textures/SCPS-55001/replacements"

# pkg_note_eol <file> <crlf|lf>: the notes are written with the Linux
# wording and forward slashes in their paths (no other slash appears in
# them); crlf turns the slashes into backslashes and adds CRs for the
# Windows package, lf leaves the text as it is
pkg_note_eol() {
    if [[ "$2" == crlf ]]; then
        sed -i -e 's#/#\\#g' -e 's/$/\r/' "$1"
    fi
}

# pkg_stage_textures_readme <dir> <crlf|lf> [serial]: writes
# <dir>/textures/<serial>/replacements/README.txt (serial SCES-50760 when
# none is given)
pkg_stage_textures_readme() {
    local serial="${3:-SCES-50760}"
    local d="$1/textures/$serial/replacements" eol="$2" disc others otherdirs
    disc="$(pkg_disc_name "$serial")"
    others="$(pkg_other_discs "$serial")"
    otherdirs="$(pkg_other_dirs textures "$serial")"
    mkdir -p "$d"
    cat > "$d/README.txt" <<TXT
This folder is for texture packs for $disc of ICO ($serial):
pictures that replace the game's own textures. Playing the game needs
nothing in it. A pack for $others goes in
$otherdirs instead; the game reads the folder of the
disc you play.
Copy a pack here keeping the pack's own folders, so its files end up under
textures/$serial/replacements/ next to the program. A pack whose
replacements folder, or loose picture files, ended up straight in
textures/ is found too; other folders of a pack put straight in textures/
are not searched.
Then start the game. Options > Display > Texture pack reads On, and
logs/ico-pc.log says how many textures were found.
With portable mode (a userdata folder next to the program) the pack may
also go in userdata/textures/$serial/replacements/.
Packs made for PCSX2 work as they are.
TXT
    fold -s -w 78 "$d/README.txt" | sed "s/ *\$//" > "$d/README.tmp" && mv "$d/README.tmp" "$d/README.txt"
    pkg_note_eol "$d/README.txt" "$eol"
}

# the models folders (model packs) and a folder for ReShade presets, with
# their notes, beside the textures folders
pkg_models_rel="models/SCES-50760/replacements"
pkg_models_rel_jpn="models/SCPS-11003/replacements"
pkg_models_rel_kor="models/SCPS-56001/replacements"
pkg_models_rel_usa="models/SCUS-97113/replacements"
pkg_models_rel_jun26="models/SCUS-97158/replacements"
pkg_models_rel_cn="models/SCPS-55001/replacements"
pkg_reshade_rel="reshade"
pkg_note_rels=("$pkg_textures_rel" "$pkg_textures_rel_jpn" "$pkg_textures_rel_kor"
    "$pkg_textures_rel_usa" "$pkg_textures_rel_jun26" "$pkg_textures_rel_cn"
    "$pkg_models_rel" "$pkg_models_rel_jpn" "$pkg_models_rel_kor"
    "$pkg_models_rel_usa" "$pkg_models_rel_jun26" "$pkg_models_rel_cn" "$pkg_reshade_rel")

# pkg_stage_models_readme <dir> <crlf|lf> [serial]: writes
# <dir>/models/<serial>/replacements/README.txt (serial SCES-50760 when none
# is given)
pkg_stage_models_readme() {
    local serial="${3:-SCES-50760}"
    local d="$1/models/$serial/replacements" eol="$2" disc others otherdirs
    disc="$(pkg_disc_name "$serial")"
    others="$(pkg_other_discs "$serial")"
    otherdirs="$(pkg_other_dirs models "$serial")"
    mkdir -p "$d"
    cat > "$d/README.txt" <<TXT
This folder is for model packs for $disc of ICO ($serial): 3D
models that replace the game's own. Playing the game needs nothing in it.
A pack for $others goes in
$otherdirs instead; the game reads the folder of the disc
you play.
Copy a pack here keeping the pack's own folders, so its files end up under
models/$serial/replacements/ next to the program. A pack whose
replacements folder, or loose files, ended up straight in models/ is found
too; other folders of a pack put straight in models/ are not searched.
Then start the game. Options > Display > Model pack (from the title
screen) reads On, and logs/ico-pc.log says how many models were found.
With portable mode (a userdata folder next to the program) the pack may
also go in userdata/models/$serial/replacements/.
To make a pack, see docs/MODEL_PACKS.md in the download's docs folder.
TXT
    fold -s -w 78 "$d/README.txt" | sed "s/ *\$//" > "$d/README.tmp" && mv "$d/README.tmp" "$d/README.txt"
    pkg_note_eol "$d/README.txt" "$eol"
}

# pkg_stage_reshade_readme <dir> <crlf|lf>: writes <dir>/reshade/README.txt
pkg_stage_reshade_readme() {
    local d="$1/$pkg_reshade_rel" eol="$2"
    mkdir -p "$d"
    if [[ "$eol" == crlf ]]; then
        cat > "$d/README.txt" <<'TXT'
This folder is a place for your ReShade presets (.ini files) and effect
files, so they stay with the game. Playing the game needs nothing in it.
Run ReShade's installer, pick ico_pc_x64.exe and choose Vulkan (or
Direct3D 12 if you set backend=d3d12 in ico-pc.ini). In ReShade's settings
you can point it at the presets in this folder.
Depth effects work: set RESHADE_DEPTH_INPUT_IS_REVERSED to 1.
For the full steps see docs/RESHADE.md in the download's docs folder
(beside the x64 folder).
TXT
    else
        cat > "$d/README.txt" <<'TXT'
On Linux this folder is a place for your vkBasalt presets (.conf files)
and effect files, so they stay with the game. Playing the game needs
nothing in it.
vkBasalt installs on your system, not here. To use a preset from this
folder, start the game with
ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE=<this folder>/<your preset>.conf
For depth effects, also set RESHADE_DEPTH_INPUT_IS_REVERSED=1 and put
depthCapture = on in the preset.
For the full steps see docs/RESHADE.md in the download's docs folder.
TXT
    fi
    pkg_note_eol "$d/README.txt" "$eol"
}

# pkg_stage_folder_notes <dir> <crlf|lf>: the folders and their notes
# (textures and models for each serial folder, and reshade)
pkg_stage_folder_notes() {
    local serial
    for serial in "${pkg_serials[@]}"; do
        pkg_stage_textures_readme "$1" "$2" "$serial"
        pkg_stage_models_readme "$1" "$2" "$serial"
    done
    pkg_stage_reshade_readme "$1" "$2"
}

# pkg_zip_dir <base> <top> <zip>: <base>/<top> and everything under it into
# <zip> (deflated, names relative to <base>, files sorted within each folder)
pkg_zip_dir() {
    python3 -I - "$1" "$2" "$3" <<'PY'
import os, sys, zipfile
base, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, _, fs in os.walk(os.path.join(base, top)):
        for f in sorted(fs):
            p = os.path.join(dp, f)
            z.write(p, os.path.relpath(p, base))
PY
}

# pkg_assert_zip_has <zip> <entry>: fails (returns 1) unless the zip lists the entry
pkg_assert_zip_has() {
    python3 -I - "$1" "$2" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    sys.exit(0 if sys.argv[2] in z.namelist() else 1)
PY
}

# pkg_assert_tar_has <tar.gz> <entry> (grep reads the whole listing: with
# pipefail, an early exit would count tar's SIGPIPE as a failure)
pkg_assert_tar_has() {
    tar -tzf "$1" | grep -xF "$2" >/dev/null
}

# pkg_assert_zip_lacks <zip> <entry>: returns 1 when the zip lists the entry
pkg_assert_zip_lacks() {
    ! pkg_assert_zip_has "$1" "$2"
}

# pkg_assert_zip_line <zip> <entry> <regex>: returns 0 when some line of the
# entry matches the (Python) regular expression, 1 otherwise or when the
# entry is missing
pkg_assert_zip_line() {
    python3 -I - "$1" "$2" "$3" <<'PY'
import re, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    try:
        data = z.read(sys.argv[2]).decode("utf-8", "replace")
    except KeyError:
        sys.exit(1)
rx = re.compile(sys.argv[3])
sys.exit(0 if any(rx.search(l) for l in re.split(r"(?<=\n)", data)) else 1)
PY
}

# pkg_assert_tar_lacks <tar.gz> <entry>: returns 1 when the archive lists the entry
pkg_assert_tar_lacks() {
    ! pkg_assert_tar_has "$1" "$2"
}

# pkg_assert_tar_line <tar.gz> <entry> <extended regex>: returns 0 when some
# line of the entry matches
pkg_assert_tar_line() {
    local t
    t="$(tar -xzOf "$1" "$2")" || return 1
    printf '%s\n' "$t" | grep -E -- "$3" >/dev/null
}
