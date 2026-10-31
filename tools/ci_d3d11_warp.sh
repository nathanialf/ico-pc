#!/usr/bin/env bash
# =============================================================================
# ci_d3d11_warp.sh
#
# The renderer's tests on Direct3D 11 through WARP (Microsoft's software
# adapter), for CI's `windows` job (.github/workflows/ci.yml; issue 126:
# the Direct3D 11 backend is exercised on no other CI device).
#
#   tools/ci_d3d11_warp.sh stage <win-x64 build dir> <out dir>
#       (Linux, the `linux` job) copies the test programs below and the DLL
#       they load (SDL3.dll: every other import is a system DLL) into one
#       flat folder, which the job uploads as an artifact.
#
#   tools/ci_d3d11_warp.sh run <dir with the programs> [<log dir>]
#       (Windows, Git Bash, the `windows` job) runs each test on Direct3D 11
#       with ICO_RHI_BACKEND=d3d11 (port/rhi/rhi_backend.c takes the default
#       backend from it) and ICO_D3D11_ADAPTER=warp (d3d11_device.c), each
#       log to <log dir>/<test>.log; a failed test prints its device line,
#       its debug-layer lines and the end of its log.  Exit 0 when every test
#       passed, 1 otherwise; a test's own 77 ("no usable device") counts as a
#       failure here, since WARP is always there; so does a "SKIP:" line in
#       a test's log (the two self-test scripts exit 0 after printing it),
#       and so does any debug-layer error or corruption line in a test's log
#       (the runtime drops the call it names, so a program can pass its own
#       checks while drawing less than it should).
#
# ICO_D3D11_DEBUG=1 asks for the debug layer; the device line says whether
# it was there ("debug layer on" / "asked for, not installed"), and its
# absence fails nothing.  The tests carry their own expectations (their
# OpenGL twins, port/render/CMakeLists.txt, prove them backend-neutral):
# a pass means Direct3D 11 draws what Vulkan and OpenGL draw.
# docs/port/TESTING.md ("The render suite on Direct3D 11 (WARP)") lists
# what runs and what does not.
# =============================================================================
set -u

# the programs (paths in the win-x64 build folder)
PROGRAMS=(
    port/rhi/rhi_d3d11_shader_plan_test.exe
    port/rhi/rhi_d3d11_test.exe
    port/render/rd_blend_selftest_test.exe
    port/render/rd_fog_selftest_test.exe
    port/render/rd_pixel_test.exe
    port/render/rd_replay_tool.exe
    port/render/rd_present_test.exe
    port/render/rd_msaa_test.exe
    port/render/rd_crt_test.exe
    port/render/rd_mirror_test.exe
)

stage() {
    local build="$1" out="$2"
    rm -rf "$out"
    mkdir -p "$out"
    for p in "${PROGRAMS[@]}"; do
        if [ ! -f "$build/$p" ]; then
            echo "ci_d3d11_warp: $build/$p was not built" >&2
            return 1
        fi
        cp "$build/$p" "$out/"
    done
    cp "$build/SDL3.dll" "$out/"
    ls -l "$out"
}

# --------------------------------------------------------------------- run
DIR=""
LOGS=""
SRC=""
WORK=""
declare -a NAMES=()
declare -a RESULTS=()
FAILED=0
# coreutils' timeout (Git Bash's /usr/bin), not Windows' own timeout.exe,
# which a PATH with System32 first would find
TIMEOUT=/usr/bin/timeout
[ -x "$TIMEOUT" ] || TIMEOUT=timeout

winpath() {
    if command -v cygpath > /dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

# the lines a failed test's reader needs first
report_failure() {
    local name="$1" log="$2"
    echo "::group::$name: device line, debug layer, log end"
    echo "-- device line:"
    grep -a -m1 "(D3D11, feature level" "$log" || echo "(no device line: rhi_init did not finish)"
    echo "-- debug layer messages (first 40):"
    grep -a -E "rhi_d3d11 debug (error|corruption|warning)" "$log" | head -n 40 || true
    echo "-- self-test lines:"
    grep -a -E "(blend|fog): (self-test|depth path|fixed-function|WARNING)|self-test failed on Direct3D 11" "$log" | head -n 20 || true
    echo "-- the log's last 150 lines:"
    tail -n 150 "$log"
    echo "::endgroup::"
}

# run_test <name> <command...>: the command's output to <name>.log, its exit
# status noted
run_test() {
    local name="$1"
    shift
    local log="$LOGS/$name.log"
    local t0 t1 rc
    t0=$(date +%s)
    echo "== $name: $*"
    (cd "$WORK" && "$TIMEOUT" 2400 "$@") > "$log" 2>&1
    rc=$?
    t1=$(date +%s)
    local verdict="passed"
    if [ "$rc" -eq 77 ]; then
        verdict="FAILED (77: no usable device)"
    elif [ "$rc" -eq 124 ]; then
        verdict="FAILED (timed out after 2400 s)"
    elif [ "$rc" -ne 0 ]; then
        verdict="FAILED (exit $rc)"
    elif grep -a -q "SKIP:" "$log"; then
        # the self-test scripts turn a program's 77 into cmake's exit 0 and
        # print "SKIP: ..."; here a skip is a failure too
        verdict="FAILED (skipped: no usable device)"
    fi
    local errs
    errs=$(grep -a -c -E "rhi_d3d11 debug (error|corruption)" "$log" || true)
    if [ "$verdict" = "passed" ] && [ "${errs:-0}" -gt 0 ]; then
        verdict="FAILED (debug-layer errors)"
    fi
    echo "   $verdict in $((t1 - t0)) s; debug-layer errors: ${errs:-0}"
    NAMES+=("$name")
    RESULTS+=("$verdict; $((t1 - t0)) s; debug-layer errors ${errs:-0}")
    if [ "$verdict" != "passed" ]; then
        FAILED=1
        report_failure "$name" "$log"
    fi
}

# log_has <name> <log> <pattern> <absent text or "">: a check on a test's
# log, noted as a row of its own: the extended regex present, the fixed
# text (when given) absent
log_has() {
    local name="$1" log="$2" want="$3" absent="$4"
    local verdict="passed"
    if [ ! -f "$log" ]; then
        verdict="FAILED (no log $log)"
    elif ! grep -a -q -E "$want" "$log"; then
        verdict="FAILED (no line matching: $want)"
    elif [ -n "$absent" ] && grep -a -q -F "$absent" "$log"; then
        verdict="FAILED (a line says: $absent)"
    fi
    echo "== $name: $verdict"
    NAMES+=("$name")
    RESULTS+=("$verdict")
    if [ "$verdict" != "passed" ]; then
        FAILED=1
        grep -a -E "VU merge|VU fuse|rd_vumerge_ab|rd_vufuse_ab" "$log" 2> /dev/null | head -n 20 || true
    fi
}

run() {
    DIR="$(cd "$1" && pwd)"
    LOGS="${2:-$DIR/logs}"
    mkdir -p "$LOGS"
    LOGS="$(cd "$LOGS" && pwd)"
    SRC="$(cd "$(dirname "$0")/.." && pwd)"
    WORK="$DIR/work"
    mkdir -p "$WORK"
    export ICO_RHI_BACKEND="${ICO_RHI_BACKEND:-d3d11}"
    export ICO_D3D11_ADAPTER="${ICO_D3D11_ADAPTER:-warp}"
    echo "ci_d3d11_warp: ICO_RHI_BACKEND=$ICO_RHI_BACKEND ICO_D3D11_ADAPTER=$ICO_D3D11_ADAPTER" \
        "ICO_D3D11_DEBUG=${ICO_D3D11_DEBUG:-} in $DIR, logs in $LOGS"

    local W
    W="$(winpath "$DIR")"
    local CMAKE_BIN
    CMAKE_BIN="$(command -v cmake || true)"

    # the CPU-only half of the shader route (no device), as on Linux
    run_test rhi_d3d11_shader_plan "$DIR/rhi_d3d11_shader_plan_test.exe"
    # the RHI's exact-texel cells on WARP, the hardware adapter (none on the
    # runner: skipped), the swapchain on a hidden window, Vulkan (none:
    # skipped); --console: no message box, the log to stdout
    run_test rhi_d3d11 "$DIR/rhi_d3d11_test.exe" --console
    # the blend's and the fog's self-tests, through the same cmake scripts
    # as ctest's rd_blend_selftest and rd_fog_selftest (without the Vulkan
    # profile line rd_blend_selftest also matches)
    if [ -n "$CMAKE_BIN" ]; then
        run_test rd_blend_selftest "$CMAKE_BIN" "-DTOOL=$W/rd_blend_selftest_test.exe" -DMODE=fixed \
            "-DNAME=fixed-function" \
            '-DRESULTS=fixed-function ok [0-2], exact modes not asked for: \[dev\] blend = attachment\|storage tries one' \
            '-DEXTRA=rd: blend: fixed-function \(the default. \[dev\] blend = attachment\|storage opts into an exact mode\)' \
            -P "$(winpath "$SRC/port/render/test/rd_blend_selftest.cmake")"
        run_test rd_fog_selftest "$CMAKE_BIN" "-DTOOL=$W/rd_fog_selftest_test.exe" -DFOG_PATH=inplace \
            "-DRESULTS=inplace ok [0-2], copy not tried, buffer not tried" \
            -P "$(winpath "$SRC/port/render/test/rd_fog_selftest.cmake")"
    else
        echo "ci_d3d11_warp: no cmake on PATH: the two self-test scripts cannot run"
        NAMES+=("rd_blend_selftest" "rd_fog_selftest")
        RESULTS+=("FAILED (no cmake)" "FAILED (no cmake)")
        FAILED=1
    fi
    # rd_pixel writes the dump rd_replay_tool replays (ctest's fixture rd_dump)
    run_test rd_pixel "$DIR/rd_pixel_test.exe" "$(winpath "$WORK")"
    if [ -f "$WORK/rd_pixel_frame.rddump" ]; then
        run_test rd_replay_tool "$DIR/rd_replay_tool.exe" "$(winpath "$WORK/rd_pixel_frame.rddump")" \
            "$(winpath "$WORK/rd_replay_tool.png")" --present 640x480 --stats --backend d3d11
        # the draw merge is on here too (issue 126: v0.9.5 and before said
        # "off on Direct3D 11" and drew one GPU draw a VU draw)
        log_has rd_replay_tool_merge_on "$LOGS/rd_replay_tool.log" "rd: VU merge: on" \
            "VU merge: off on Direct3D 11"
        # the draw merge and the fused scissor pair against one GPU draw a
        # VU draw and the kicks under their own pipelines, byte for byte, on
        # the frames rd_pixel leaves (port/render/CMakeLists.txt
        # rd_vumerge_ab_pixel and rd_vufuse_ab_pixel run the same on Vulkan):
        # rd_pixel_vu (64 code-32 draws of one state: the merge joins them),
        # rd_pixel_vu36 (a code-36 cut and kick pair) and rd_pixel_frame (no
        # VU draw). The 16:9 and phone settings skip for these 4:3 frames.
        if [ -n "$CMAKE_BIN" ]; then
            printf '%s\n' rd_pixel_vu.rddump rd_pixel_vu36.rddump rd_pixel_frame.rddump \
                > "$WORK/vu_pixel_dumps.txt"
            run_test rd_vumerge_ab "$CMAKE_BIN" "-DTOOL=$W/rd_replay_tool.exe" \
                "-DLIST=$(winpath "$WORK/vu_pixel_dumps.txt")" "-DOUT=$(winpath "$WORK/vumerge_ab")" \
                -P "$(winpath "$SRC/port/render/test/rd_vumerge_ab.cmake")"
            # some draws merged (the script's own gate is fewer GPU draws in all)
            log_has rd_vumerge_ab_joined "$LOGS/rd_vumerge_ab.log" \
                "\([1-9][0-9]* draws merged into [1-9]" ""
            # a merge build the driver refuses (faked) turns the merge off
            # from the next replay: the later replays draw one GPU draw a VU draw
            run_test rd_vumerge_fallback "$CMAKE_BIN" "-DTOOL=$W/rd_replay_tool.exe" \
                "-DDUMP=$(winpath "$WORK/rd_pixel_vu.rddump")" \
                "-DOUT=$(winpath "$WORK/vumerge_fallback")" \
                -P "$(winpath "$SRC/port/render/test/rd_vumerge_fallback.cmake")"
            run_test rd_vufuse_ab "$CMAKE_BIN" "-DTOOL=$W/rd_replay_tool.exe" \
                "-DLIST=$(winpath "$WORK/vu_pixel_dumps.txt")" "-DEXTRA=" \
                "-DOUT=$(winpath "$WORK/vufuse_ab")" \
                -P "$(winpath "$SRC/port/render/test/rd_vufuse_ab.cmake")"
        else
            echo "ci_d3d11_warp: no cmake on PATH: the merge and fuse A/Bs cannot run"
            NAMES+=("rd_vumerge_ab" "rd_vumerge_fallback" "rd_vufuse_ab")
            RESULTS+=("FAILED (no cmake)" "FAILED (no cmake)" "FAILED (no cmake)")
            FAILED=1
        fi
    else
        echo "ci_d3d11_warp: rd_pixel left no rd_pixel_frame.rddump: rd_replay_tool not run"
        NAMES+=("rd_replay_tool")
        RESULTS+=("FAILED (no dump from rd_pixel)")
        FAILED=1
    fi
    run_test rd_present "$DIR/rd_present_test.exe" "$(winpath "$WORK")"
    run_test rd_msaa "$DIR/rd_msaa_test.exe"
    run_test rd_crt "$DIR/rd_crt_test.exe" "$(winpath "$WORK")"
    run_test rd_mirror "$DIR/rd_mirror_test.exe"

    # the summary: the device line once (the first log that has one) and
    # whether the debug layer was there, then the verdicts
    local dev
    dev=$(cat "$LOGS"/*.log 2> /dev/null | grep -a -m1 "(D3D11, feature level" || true)
    {
        echo "### Direct3D 11 on WARP"
        echo
        echo "Device: \`${dev:-no device line in any log}\`"
        echo
        echo "| test | result |"
        echo "| --- | --- |"
        for i in "${!NAMES[@]}"; do
            echo "| ${NAMES[$i]} | ${RESULTS[$i]} |"
        done
    } > "$LOGS/summary.md"
    cat "$LOGS/summary.md"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        cat "$LOGS/summary.md" >> "$GITHUB_STEP_SUMMARY"
    fi
    return "$FAILED"
}

case "${1:-}" in
stage)
    [ $# -eq 3 ] || {
        echo "usage: $0 stage <win-x64 build dir> <out dir>" >&2
        exit 2
    }
    stage "$2" "$3"
    ;;
run)
    [ $# -ge 2 ] || {
        echo "usage: $0 run <dir> [<log dir>]" >&2
        exit 2
    }
    run "$2" "${3:-}"
    ;;
*)
    echo "usage: $0 stage <win-x64 build dir> <out dir> | run <dir> [<log dir>]" >&2
    exit 2
    ;;
esac
