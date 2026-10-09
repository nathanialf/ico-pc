# shellcheck shell=bash
# =============================================================================
# tools/fetch_common.sh
#
# The helpers and pins tools/fetch_toolchain.sh, tools/fetch_deps.sh and
# tools/fetch_android.sh share. Sourced, not run; fetch_deb downloads into
# the caller's $TMP.
# =============================================================================

# SDL3: one release for the desktop and Android builds (tools/notices/
# manifest.json names it). The SHA-256 is the digest GitHub lists for the
# release's source tarball.
SDL3_VERSION="${SDL3_VERSION:-3.4.18}"
SDL3_SRC_SHA256="${SDL3_SRC_SHA256:-9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3}"

# The build host: x86-64 or arm64 (aarch64) Linux. ICO_HOST_ARCH names the
# host's directories under tools/toolchain/deps (sdl3/linux-x64,
# sdl3/linux-arm64), DEB_ARCH picks the pinned Debian package of each pair
# and DEB_MULTIARCH is that package's library directory under usr/lib.
case "$(uname -m)" in
    x86_64) ICO_HOST_ARCH=x64 DEB_ARCH=amd64 DEB_MULTIARCH=x86_64-linux-gnu ;;
    aarch64 | arm64) ICO_HOST_ARCH=arm64 DEB_ARCH=arm64 DEB_MULTIARCH=aarch64-linux-gnu ;;
    *)
        echo "fetch: only x86_64 and aarch64 Linux hosts are pinned here ($(uname -m))" >&2
        exit 1
        ;;
esac

# by_arch <x64 value> <arm64 value>: the one for this host.
by_arch() {
    if [[ "$ICO_HOST_ARCH" == "arm64" ]]; then echo "$2"; else echo "$1"; fi
}

# Debian packages are pinned by version and SHA-256; deb.debian.org drops
# superseded versions, so snapshot.debian.org is the fallback.
DEB_SNAPSHOT="${DEB_SNAPSHOT:-https://snapshot.debian.org/archive/debian/20261004T000000Z}"

# fetch <url> <sha256> <file>: download and verify.
fetch() {
    echo "==> fetching $1"
    curl -fL --retry 3 -o "$3" "$1"
    echo "${2}  $3" | sha256sum -c -
}

# stamped <dir> <id>: true when <dir> already holds release <id>.
stamped() {
    [[ -f "$1/.ico-release" && "$(cat "$1/.ico-release")" == "$2" ]]
}

# fetch_deb "<pool path> <sha256>" <dir>: download a pinned Debian package
# (deb.debian.org, else DEB_SNAPSHOT), verify it and unpack it into <dir>
# (dpkg-deb -x, no root).
fetch_deb() {
    local path="${1% *}" sum="${1#* }" dir="$2" deb
    deb="$TMP/$(basename "$path")"
    echo "==> fetching $(basename "$path")"
    if ! curl -fsL --retry 3 -o "$deb" "http://deb.debian.org/debian/${path//+/%2B}"; then
        curl -fsL --retry 3 -o "$deb" "${DEB_SNAPSHOT}/${path//+/%2B}"
    fi
    echo "${sum}  ${deb}" | sha256sum -c -
    dpkg-deb -x "$deb" "$dir"
    rm -f "$deb"
}
