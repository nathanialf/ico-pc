#!/usr/bin/env bash
# CI's host packages for one job kind: ci_host_packages.sh <x64|arm64|android> [--list]
#
# The runner's package mirror is sometimes so slow that a plain
# `apt-get install` runs into the step's time limit (2026-11-18: 10.8 MB in
# three minutes; six of eleven runs lost jobs to it). So:
#   - the downloaded .deb files live in a directory the workflow caches
#     between runs (.github/actions/host-packages): a warm run downloads
#     nothing, or only what Ubuntu updated since;
#   - the index update and the download are tried three times, each try under
#     its own time limit, the second and third on other mirrors; a download
#     that was cut off continues where it stopped;
#   - the unpack runs once, after the downloads, without a time limit of its
#     own (a package manager killed half way leaves the runner broken).
# The step's own limit in the workflow stays the outer bound: three tries of
# 45 s + 100 s with their kill grace are under eight minutes of its twelve,
# which leaves the unpack its time.
set -u

kind=${1:-}
x11="libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
libxfixes-dev libxrender-dev libasound2-dev libpulse-dev \
libpipewire-0.3-dev libspa-0.2-dev libdbus-1-dev libxss-dev \
libgl-dev libglx-dev libegl-dev libgl1 libegl1 libgl1-mesa-dri \
libvulkan1 mesa-vulkan-drivers"
# lavapipe (mesa-vulkan-drivers): the render tests run on the CPU Vulkan
# driver instead of skipping. llvmpipe (libgl1-mesa-dri, libgl1, libegl1):
# the OpenGL tests run on the CPU OpenGL driver; the GL headers build SDL
# with GLX and EGL.
case $kind in
x64) pkgs="build-essential gcc-14 curl xz-utils python3 python3-venv $x11" ;;
arm64) pkgs="build-essential gcc-14 g++ git ccache curl xz-utils python3 python3-venv $x11 \
libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev libdrm-dev libgbm-dev" ;;
android) pkgs="build-essential curl xz-utils python3 python3-venv" ;;
*)
    echo "usage: $0 <x64|arm64|android> [--list]" >&2
    exit 2
    ;;
esac
if [ "${2:-}" = --list ]; then
    printf '%s\n' $pkgs
    exit 0
fi

debs=${ICO_APT_ARCHIVES:-$HOME/.cache/ico-apt}
update_s=${ICO_APT_UPDATE_S:-45}
download_s=${ICO_APT_DOWNLOAD_S:-100}
mkdir -p "$debs/partial"
sudo chown -R root:root "$debs"
opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 \
-o DPkg::Lock::Timeout=60 -o Dir::Cache::Archives=$debs"

# the mirrors a later try moves to (the runner's own list is the first try's)
if [ "$(dpkg --print-architecture)" = amd64 ]; then
    alt2=http://azure.archive.ubuntu.com/ubuntu/
    alt3=http://archive.ubuntu.com/ubuntu/
else
    alt2=http://azure.ports.ubuntu.com/ubuntu-ports/
    alt3=http://ports.ubuntu.com/ubuntu-ports/
fi
mirrors=/etc/apt/apt-mirrors.txt
use_mirror() {
    if [ -f "$mirrors" ]; then
        echo "host packages: the mirror list was:"
        sed 's/^/    /' "$mirrors"
        printf '%s\tpriority:1\n' "$1" | sudo tee "$mirrors" > /dev/null
    else
        # no mirror list on this image: the sources name the archive directly
        sudo sed -i -E "s#^(URIs:[[:space:]]*).*ubuntu(-ports)?/?[[:space:]]*\$#\\1$1#" \
            /etc/apt/sources.list.d/ubuntu.sources
    fi
    echo "host packages: now on $1"
}

ok=0
for try in 1 2 3; do
    [ "$try" = 2 ] && use_mirror "$alt2"
    [ "$try" = 3 ] && use_mirror "$alt3"
    t0=$(date +%s)
    # shellcheck disable=SC2086
    if sudo timeout -k 10 "$update_s" apt-get $opts update &&
        sudo timeout -k 10 "$download_s" apt-get $opts install -y --no-install-recommends \
            --download-only $pkgs; then
        ok=1
        echo "host packages: try $try: index and downloads in $(($(date +%s) - t0)) s"
        break
    fi
    echo "host packages: try $try failed or ran past its limit after $(($(date +%s) - t0)) s" \
        "(index ${update_s} s, downloads ${download_s} s)"
done
if [ "$ok" != 1 ]; then
    echo "::error::host packages: three tries failed (the runner's mirror, then $alt2, then $alt3)"
    exit 1
fi

# shellcheck disable=SC2086
sudo apt-get $opts install -y --no-install-recommends $pkgs || exit 1

# the cache keeps what this job kind installs today: files no mirror offers
# any more (older versions) go, and the directory's file names are its key
# shellcheck disable=SC2086
sudo apt-get $opts autoclean > /dev/null || true
sudo rm -rf "$debs/partial" "$debs/lock"
sudo chown -R "$(id -u):$(id -g)" "$debs"
names=$(cd "$debs" && ls -1 | LC_ALL=C sort | sha256sum | cut -c1-16)
echo "host packages: $(ls -1 "$debs" | wc -l) package files kept for the next run (names $names)"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "names=$names" >> "$GITHUB_OUTPUT"
fi
