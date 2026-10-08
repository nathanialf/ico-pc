# tools/package_textures_lib.sh: the textures folder of the release packages,
# sourced by package_win.sh and package_linux.sh and by
# tools/test/package_textures_test.sh. Functions only.

# the folder under the program folder the game looks in, and the note in it
pkg_textures_rel="textures/SCES-50760/replacements"

# pkg_stage_textures_readme <dir> <crlf|lf>: writes <dir>/textures/SCES-50760/replacements/README.txt
pkg_stage_textures_readme() {
    local d="$1/$pkg_textures_rel" eol="$2"
    mkdir -p "$d"
    cat > "$d/README.txt" <<'TXT'
This folder is for texture packs: pictures that replace the game's own
textures. Playing the game needs nothing in it.
Copy a pack here keeping the pack's own folders, so its files end up under
textures\SCES-50760\replacements\ next to the program. A pack copied one
level too high, into textures\, is found too.
Then start the game. Options > Display > Texture pack reads On, and
logs\ico-pc.log says how many textures were found.
With portable mode (a userdata folder next to the program) the pack may
also go in userdata\textures\SCES-50760\replacements\.
Packs made for PCSX2 work as they are.
TXT
    if [[ "$eol" == crlf ]]; then
        sed -i 's/$/\r/' "$d/README.txt"
    fi
}

# the models folder (model packs) and a folder for ReShade presets, with
# their notes, beside the textures folder
pkg_models_rel="models/SCES-50760/replacements"
pkg_reshade_rel="reshade"
pkg_note_rels=("$pkg_textures_rel" "$pkg_models_rel" "$pkg_reshade_rel")

# pkg_stage_models_readme <dir> <crlf|lf>: writes <dir>/models/SCES-50760/replacements/README.txt
pkg_stage_models_readme() {
    local d="$1/$pkg_models_rel" eol="$2"
    mkdir -p "$d"
    cat > "$d/README.txt" <<'TXT'
This folder is for model packs: 3D models that replace the game's own.
Playing the game needs nothing in it.
Copy a pack here keeping the pack's own folders, so its files end up under
models\SCES-50760\replacements\ next to the program.
Then start the game. Options > Display > Model pack (from the title
screen) reads On, and logs\ico-pc.log says how many models were found.
With portable mode (a userdata folder next to the program) the pack may
also go in userdata\models\SCES-50760\replacements\.
To make a pack, see docs\MODEL_PACKS.md.
TXT
    if [[ "$eol" == crlf ]]; then
        sed -i 's/$/\r/' "$d/README.txt"
    fi
}

# pkg_stage_reshade_readme <dir> <crlf|lf>: writes <dir>/reshade/README.txt
pkg_stage_reshade_readme() {
    local d="$1/$pkg_reshade_rel" eol="$2"
    mkdir -p "$d"
    cat > "$d/README.txt" <<'TXT'
This folder is a place for your ReShade presets (.ini files) and shader
packs, so they stay with the game. Playing the game needs nothing in it.
ReShade itself installs next to the program: run its installer, pick
ico_pc_x64.exe and choose Vulkan (or Direct3D 12 if you set backend=d3d12
in ico-pc.ini). In ReShade's settings you can point it at the presets
in this folder.
Depth effects work: set RESHADE_DEPTH_INPUT_IS_REVERSED to 1.
For the full steps see docs\RESHADE.md.
TXT
    if [[ "$eol" == crlf ]]; then
        sed -i 's/$/\r/' "$d/README.txt"
    fi
}

# pkg_stage_folder_notes <dir> <crlf|lf>: the three folders and their notes
pkg_stage_folder_notes() {
    pkg_stage_textures_readme "$1" "$2"
    pkg_stage_models_readme "$1" "$2"
    pkg_stage_reshade_readme "$1" "$2"
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
