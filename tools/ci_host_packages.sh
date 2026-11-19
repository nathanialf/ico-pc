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
#   - a runner whose list names Ubuntu's EC2 mirror starts on the main
#     archive instead, and each cached file is checked against the index's
#     SHA-256 before it is installed;
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

cache=${ICO_APT_ARCHIVES:-$HOME/.cache/ico-apt}
debs=$cache
lists=$cache/lists
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

# A runner whose list names Ubuntu's EC2 mirror (GitHub's AWS-hosted runners
# since 2026-10-28, actions/runner-images issue 14823): that mirror trickles
# at 60-110 kB/s and apt does not leave a slow mirror by itself, so the main
# archive is the first try there and the runner's own list the last.
ec2=0
if [ -f "$mirrors" ] && grep -q 'ec2\.\(archive\|ports\)\.ubuntu\.com' "$mirrors"; then
    ec2=1
    sudo cp "$mirrors" "$mirrors.runner"
    echo "host packages: the runner's list names the EC2 mirror: $alt3 first, the runner's own list last"
fi

# The cached package files against the index's SHA-256, before any of them is
# installed: a file whose sum is not the index's goes (the tries below fetch
# it again). A file the index does not know (an older version) is left for
# the autoclean at the end; apt does not install it.
verify_debs() {
    bad=0
    n=0
    for f in "$debs"/*.deb; do
        [ -f "$f" ] || continue
        pkg=$(dpkg-deb -f "$f" Package 2> /dev/null) || pkg=
        ver=$(dpkg-deb -f "$f" Version 2> /dev/null) || ver=
        want=
        if [ -n "$pkg" ] && [ -n "$ver" ]; then
            want=$(apt-cache show "$pkg=$ver" 2> /dev/null | awk '/^SHA256:/ { print $2; exit }')
        fi
        if [ -n "$pkg" ] && [ -z "$want" ]; then
            continue
        fi
        n=$((n + 1))
        if [ -z "$pkg" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$want" ]; then
            bad=$((bad + 1))
            echo "host packages: $(basename "$f") is not the file the index names (SHA-256): removed"
            sudo rm -f "$f"
        fi
    done
    echo "host packages: $n cached package files checked against the index's SHA-256, $bad removed"
}

# First without the network at all: the package index the last good run kept
# beside the package files. Two runners of run 44480916957 could not use
# Ubuntu's servers (one never read the Ubuntu source entry, one got no answer
# from three mirrors in 45 s each) while every package was already in the
# cache. An index older than the image's own packages can refuse the set:
# then the tries below run as before.
ok=0
if [ -d "$lists" ] && ls "$debs"/*.deb > /dev/null 2>&1; then
    sudo mkdir -p /var/lib/apt/lists.runner
    sudo sh -c 'mv /var/lib/apt/lists/* /var/lib/apt/lists.runner/ 2>/dev/null; cp -a "$1"/. /var/lib/apt/lists/' sh "$lists"
    verify_debs
    # shellcheck disable=SC2086
    if sudo apt-get $opts install -y --no-install-recommends --no-download $pkgs; then
        ok=2
        echo "host packages: installed from the cached index and package files, nothing downloaded"
    else
        echo "host packages: the cached index and files do not install on this image; the network tries follow"
        sudo sh -c 'rm -rf /var/lib/apt/lists/*; mv /var/lib/apt/lists.runner/* /var/lib/apt/lists/ 2>/dev/null' || true
    fi
fi

# the runner's Ubuntu source entry, written anew for a mirror: for a runner
# whose own entry is not read (no Ubuntu line in its update's output)
own_source() {
    . /etc/os-release
    printf 'Types: deb\nURIs: %s\nSuites: %s %s-updates %s-backports %s-security\nComponents: main universe restricted multiverse\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n' \
        "$1" "$VERSION_CODENAME" "$VERSION_CODENAME" "$VERSION_CODENAME" "$VERSION_CODENAME" |
        sudo tee /etc/apt/sources.list.d/ico-ubuntu.sources > /dev/null
    echo "host packages: the runner's sources (no Ubuntu package had a candidate):"
    ls -la /etc/apt/sources.list.d/ | sed 's/^/    /'
    echo "host packages: own source entry written for $1"
}

for try in 1 2 3; do
    [ "$ok" = 0 ] || break
    [ "$try" = 1 ] && [ "$ec2" = 1 ] && use_mirror "$alt3"
    [ "$try" = 2 ] && use_mirror "$alt2"
    if [ "$try" = 3 ] && [ "$ec2" = 1 ]; then
        sudo cp "$mirrors.runner" "$mirrors"
        echo "host packages: now on the runner's own list again"
    elif [ "$try" = 3 ]; then
        use_mirror "$alt3"
    fi
    if [ "$try" != 1 ] && ! apt-cache policy build-essential 2> /dev/null | grep -q 'Candidate: [0-9]'; then
        [ "$try" = 2 ] && own_source "$alt2"
        [ "$try" = 3 ] && own_source "$alt3"
    fi
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
if [ "$ok" = 0 ]; then
    echo "::error::host packages: three tries failed (the runner's mirror or, where it is the EC2 one, $alt3 first; then $alt2; then $alt3 or the runner's own)"
    exit 1
fi

if [ "$ok" = 1 ]; then
    # shellcheck disable=SC2086
    verify_debs
    sudo apt-get $opts install -y --no-install-recommends $pkgs || exit 1
    # the index this install used, for the next run's try without the network
    sudo rm -rf "$lists"
    sudo mkdir -p "$lists"
    sudo sh -c 'cp -a /var/lib/apt/lists/. "$1"/ && rm -rf "$1"/partial "$1"/lock "$1"/auxfiles' sh "$lists"
fi

# the cache keeps what this job kind installs today: files no mirror offers
# any more (older versions) go, and the directory's file names are its key
# shellcheck disable=SC2086
sudo apt-get $opts autoclean > /dev/null || true
sudo rm -rf "$debs/partial" "$debs/lock"
sudo chown -R "$(id -u):$(id -g)" "$debs"
names=$(cd "$debs" && ls -1 *.deb | LC_ALL=C sort | sha256sum | cut -c1-16)
echo "host packages: $(ls -1 "$debs"/*.deb | wc -l) package files and their index kept for the next run (names $names)"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "names=$names" >> "$GITHUB_OUTPUT"
fi
