# tools/package_docs_lib.sh: the player guides of the release packages
# (docs/*.md that README.md links), sourced by package_win.sh and
# package_linux.sh. Functions only. BUILDING.md, LEGAL.md and THIRD_PARTY.md
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
