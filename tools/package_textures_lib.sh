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

# pkg_assert_zip_has <zip> <entry>: fails (returns 1) unless the zip lists the entry
pkg_assert_zip_has() {
    python3 -I - "$1" "$2" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    sys.exit(0 if sys.argv[2] in z.namelist() else 1)
PY
}

# pkg_assert_tar_has <tar.gz> <entry>
pkg_assert_tar_has() {
    tar -tzf "$1" | grep -qxF "$2"
}
