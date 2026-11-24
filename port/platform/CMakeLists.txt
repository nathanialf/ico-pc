# port/platform/CMakeLists.txt: the platform sources ico_platform takes
# beyond the top-level CMakeLists.txt's, and the platform tests.

target_sources(ico_platform PRIVATE
    app_icon.c
    app_icon_ico.c
    app_icon_image.c
    arena.c
    backup.c
    bones_ladder.c
    clock_trap.c
    code_hash.c
    core_share.c
    cpu_keep.c
    diag_host.c
    exit_info.c
    fiber.c
    fps_counter.c
    frame_mailbox.c
    game_data_range.c
    gpu_hang.c
    gpu_sysfs.c
    thermal_text.c
    hotkeys.c
    kernel_host.c
    last_run.c
    last_run_skin.c
    vu_zero_mark.c
    nodiscard_mark.c
    fastps_mark.c
    compact_mark.c
    ratecard_env.c
    ahead_mark.c
    log_cost.c
    mem_breakdown.c
    mem_watch.c
    pace_policy.c
    perf_hint.c
    perf_secs.c
    power_read.c
    memory_floor.c
    display_rate.c
    present_hist.c
    present_latency.c
    present_stall_block.c
    perf_block.c
    pin_release.c
    pipeline_skip.c
    pipe_resume.c
    present_fallback.c
    set_size.c
    pump_watch.c
    report.c
    robust_persist.c
    sched.c
    stall_coincide.c
    startup_retry.c
    state_check.c
    state_file.c
    state_hash.c
    state_registry.c
    state_reloc.c
    state_syms.c
    state_legacy.c
    state_xbuild.c
    sys_cell.c
    thermal_read.c
    symmap.c
    vm_reserve.c
    vmstat_read.c
    vsync_hooks.c
    window_lifecycle.c
    window_scene.c)
# diag_host.c's heartbeat and watchdog thread (pthreads on POSIX).
find_package(Threads REQUIRED)
target_link_libraries(ico_platform PUBLIC Threads::Threads)
# report.c deflates the report file's entries (tdefl) and symmap.c reads a
# gzip symbol table (tinfl), both into static buffers
target_link_libraries(ico_platform PUBLIC ico_miniz)
if(TARGET ico_pc)
    # A link map beside the program, shipped with test builds, to turn the
    # crash report's executable offsets into symbols.
    if(APPLE)
        target_link_options(ico_pc PRIVATE "LINKER:-map,$<TARGET_FILE_DIR:ico_pc>/ico_pc.map")
    else()
        target_link_options(ico_pc PRIVATE "LINKER:-Map=$<TARGET_FILE_DIR:ico_pc>/ico_pc.map")
    endif()
endif()
# host_loop.c calls into the game (its main, ico_vsync, systemStatus), so it
# goes into the program beside main_host.c, ahead of the game library on the
# link line, rather than into ico_platform, which the game itself needs.
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE host_loop.c)
endif()
# game_data_verify.c names the game's globals (the save states' start-up
# check of the game data ranges, game_data_range.h): the program only
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE game_data_verify.c)
endif()
# state_port.c registers the port's save state blocks (state_port.h), which
# live in libraries the platform layer does not link: the program only
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE state_port.c)
endif()
# The commit the program was built from (ICO_BUILD_COMMIT in the
# generated ico_build_commit.h, "git describe --always --dirty"), for the pad
# recording's header (main_host.c). Checked at every build; the header only
# changes when the commit does.
if(TARGET ico_pc)
    set(_ico_commit_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_target(ico_build_commit
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_SOURCE_DIR}
                -DOUT=${_ico_commit_dir}/ico_build_commit.h
                -P "${CMAKE_CURRENT_SOURCE_DIR}/build_commit.cmake"
        BYPRODUCTS "${_ico_commit_dir}/ico_build_commit.h"
        COMMENT "Build commit"
        VERBATIM)
    add_dependencies(ico_pc ico_build_commit)
    target_include_directories(ico_pc PRIVATE "${_ico_commit_dir}")
endif()
# port/null/gfx_null.c (the graphics floor under the game's packet code, in
# both builds) is linked into ico_pc by the top-level CMakeLists.txt.

# ICO_HEAP_STATS: memory.c reports allocations to arena.c, which logs each
# partition's high-water mark.
option(ICO_HEAP_STATS "Log the game heap's per-partition high-water marks" OFF)
if(ICO_HEAP_STATS)
    target_compile_definitions(ico_fumi PRIVATE ICO_HEAP_STATS=1)
endif()

# --- Tests ------------------------------------------------------------------
# startup_retry.c: the steps after a graphics driver crash while building a
# pipeline, and the relaunch's environment
add_executable(startup_retry_test test/startup_retry_test.c)
target_compile_options(startup_retry_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(startup_retry_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME startup_retry COMMAND startup_retry_test)
endif()
# memory_floor: the start-up set's free-memory floor (memory_floor.c, issue
# 161): the default floor, the reporter's figures, a small device, the setting
# pipeline_skip: the record of a pipeline family a device's driver stopped in
# (pipeline_skip.c, issue 161): the note's family, the value written and read
# back, the device and driver match, the [dev] clear
# robust_persist: the port's own [dev] gpu_robust turning itself off (robust_persist.c):
# the rule over sequences of starts, the mark written and read back through the
# config text, the key removal, the log lines word for word
# gpu_hang: the GPU hang limit's arithmetic and its watch (gpu_hang.c, issue 123)
# state_fiber: save states' fiber pool, a suspended fiber saved and restored
# into a second pool, the dead frames under its stack pointer kept, and the
# relocation words (fiber.c, state_reloc.c)
# state_guard: save states' two process values (the thread pointer and the
# stack guard) rewritten old -> new by exact match in words, a scan and a
# real fiber's registers and frame, and the per-platform refusal of a state
# without them (state_reloc.c, fiber.c)
# state_port: save states across versions (state_syms.c, code_hash.c,
# state_xbuild.c, state_reloc.c, state_file.c): two synthetic builds' symbol
# files and code, the masked code hashes, the translation of every word and
# the refusal naming the first blocker (a changed function, the data
# layout, a block), the bounds' fallback, the format 1 refusal; nothing of
# the game starts
# state_name: every address a save state holds is named across versions
# (state_xbuild.c, state_syms.c, issue 217): synthetic images laid out as
# Android's library (one read and execute range, the symbol file's section
# lines), Linux's and Windows' programs carry a state's words (start-up
# values, texts of any length and encoding, an anonymous table's word,
# statics of one name in two files) into a build where each moved; a text
# twice or missing there, a code word no unit holds (its listed line), a
# window longer than the record's pool, version 2 records, a GNU ld map's
# output sections
# state_registry: the save state registry's table (state_registry.c)
# display_rate: the refresh rate's log lines and the Android frame rate pin's
# decisions (display_rate.c, issue 123)
# present_hist: the 10 s block's histogram of the driver's present call
# (present_hist.c, issue 123)
# present_stall_block: the present stalls per 10 s block and per run against
# the block's GPU span band and the display's rate (present_stall_block.c,
# issue 123): the issue 123 log's figures, the lines word for word, a clock
# restart, the run's per-minute sums, a short buffer
# gpu_sysfs: the kernel GPU driver's nodes, the pure part (gpu_sysfs.c, issue
# 123): the parser over the nodes' texts, the ring, the block and around
# reductions, the three lines word for word
# present_fallback: the FIFO fallback's decision over a log's present blocks
# under mailbox, the driver's eligibility, the [dev] value and the lines
# (present_fallback.c)
# frame_mailbox: the game thread's and the render thread's hand-over
# (frame_mailbox.c): the latest frame wins, the slot pins, park and quit with
# a job queued, the submit's handshake, the heartbeat and the block's
# numbers under a fake clock, the [dev] render_thread mode
# pipe_resume: the start-up set resumed after a low-memory stop (pipe_resume.c,
# issue 123): the stop line's part word for word and read back, an older
# version's line without it, the batch halving across starts (32 -> 16 -> 8
# -> 4, floor 4), the place inside a set that shrank, the stop count, the
# longest line against the activity file's 256 bytes
# clock_trap: one picture a tick behind the step rule (clock_trap.c, the
# owner's Pixel in the cage room): the step rule's rate at a picture's reads
# of 8 to 30 ms in the cage's frame (pace_policy.c pace_rt_further_trace: two
# a tick up to the bound 2R <= 3P - 2S - margin, one past it), the seconds
# (a second's pictures, frames, asks and holds; a gap restarts), the rule
# (one a tick, wanted, held) said at each such second's close, the block's
# worst second, the clusters and the dip against the median of the blocks
# before (the owner's log's clocks: the dip at :4251 and :5276, none in the
# blocks between), the lines word for word, one second's text, the blocks'
# picture CPU median and a second's CPU raised against it
# pin_release: the render and game threads' single-core pins let go at one
# picture a tick (pin_release.c, the owner's Pixel, v0.11.7-rc2's seconds):
# stuck = one a tick with the CPU raised, let go and taken again after 10
# judged seconds without, a stuck second starting the count again, the rule
# not allowing it, the trace's seconds and its cap, the threads' cores a
# second and a block, every line word for word
# perf_secs: pictures a second by stage (perf_secs.c, issue 179): a block's
# whole seconds under the stage of each second's first step, a hitch's
# second, a stage change inside a second, two stages in one 10 s block (the
# block not cut: hotkeys_test), the part second, pictures before any step, seconds past
# the 64 held, a short buffer, the lines word for word
# bones_ladder: issue 123's ladder across starts (bones_ladder.c): the step
# for each run's evidence over sequences of starts (step 1, step 2, healed,
# both tried, a port race, step 2 out of reach), the mark written and read
# back, the start and action lines word for word
foreach(t sched fiber fps_counter arena diag hotkeys pace_policy pace_loop perf_block lifecycle pump_watch ico_thread
          gpu_hang state_fiber state_guard state_port state_name state_registry pipeline_skip
          memory_floor present_fallback frame_mailbox display_rate present_hist present_stall_block gpu_sysfs
          thermal_text window_scene bones_ladder pipe_resume clock_trap pin_release
          perf_secs)
    add_executable(${t}_test test/${t}_test.c)
    target_include_directories(${t}_test PRIVATE "${ICO_COMPAT_DIR}")
    target_compile_options(${t}_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(${t}_test PRIVATE ico_platform)
    if(ICO_RUN_TESTS)
        add_test(NAME ${t} COMMAND ${t}_test)
    endif()
endforeach()
# display_rate.c rounds a rate with floor: the math library where it is separate
if(UNIX)
    target_link_libraries(display_rate_test PRIVATE m)
endif()
# diag_test's own link map beside it: its crash cases check the symbol
# lines against it, as a player's ico_pc.map is read beside ico_pc
if(NOT APPLE AND NOT WIN32)
    target_link_options(diag_test PRIVATE "LINKER:-Map=$<TARGET_FILE_DIR:diag_test>/diag_test.map")
endif()
if(ICO_RUN_TESTS)
    set_tests_properties(diag PROPERTIES SKIP_RETURN_CODE 77)
    # the switch's step lines (ico_diag_step) and its watch
    # (ico_diag_watchdog_switch): a killed child's log names the step, a
    # hung step ends the run with the report naming it, steady progress does
    # not (diag_test --switch; POSIX only, 77 elsewhere)
    add_test(NAME diag_switch COMMAND diag_test --switch)
    set_tests_properties(diag_switch PROPERTIES SKIP_RETURN_CODE 77)
endif()

# state_legacy: the committed legacy tables (port/state_legacy, issue 217)
# read by the game's loader (state_legacy.c): each table the index names
# loads with its manifest's counts, its code hashes in address order under
# the current rules, every function its symbol file cuts hashed; the index's
# lookup (a release, an rc, another platform, an unknown commit, no index), a
# missing and a cut file; the source tree's folder, no game
add_executable(state_legacy_test test/state_legacy_test.c)
target_include_directories(state_legacy_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(state_legacy_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(state_legacy_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME state_legacy COMMAND state_legacy_test "${CMAKE_SOURCE_DIR}/port/state_legacy")
endif()

# cpu_place: the cores a thread ran on most, the cores' speeds from a fake
# sysfs under the build folder (Linux and Android only), the 10 s part's
# words, the masks' text, the calling thread's priority and allowed cores,
# its CPU time, the threads line from a fake table and a fake
# /proc/self/task
add_executable(cpu_place_test test/cpu_place_test.c)
target_compile_options(cpu_place_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(cpu_place_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME cpu_place COMMAND cpu_place_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# thread_stat: a thread's time on a core and waiting for one (thread_stat.c,
# issue 179): the kernel's schedstat text, a fake /proc/self/task under the
# build folder (Linux and Android), the block's difference, the words
add_executable(thread_stat_test test/thread_stat_test.c)
target_compile_options(thread_stat_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(thread_stat_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME thread_stat COMMAND thread_stat_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The Android build's file layout (android/android_paths.c), pure C, so it
# is tested here on the native builds.
add_executable(android_paths_test test/android_paths_test.c android/android_paths.c)
target_include_directories(android_paths_test PRIVATE android)
target_compile_options(android_paths_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME android_paths COMMAND android_paths_test)
endif()

# The Android build's graphics driver packages (gpu_driver.c),
# pure C over host_fs, miniz and json.h, so it is tested on the native builds;
# it writes under the build folder
add_executable(gpu_driver_test test/gpu_driver_test.c gpu_driver.c)
target_compile_options(gpu_driver_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(gpu_driver_test PRIVATE ico_host_fs ico_json ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME gpu_driver COMMAND gpu_driver_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Extras > Add to Steam (steam_shortcut.c): the binary VDF reader and writer,
# the Steam folder search and the running-Steam check, over a fake Steam
# folder under the build folder
add_executable(steam_shortcut_test test/steam_shortcut_test.c steam_shortcut.c)
target_include_directories(steam_shortcut_test PRIVATE .)
target_compile_options(steam_shortcut_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(steam_shortcut_test PRIVATE ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME steam_shortcut COMMAND steam_shortcut_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The application icon (app_icon.h): the image maths on synthetic frames, the
# .ico writer parsed back, and the files and stamp over a fake user folder
# under the build folder
add_executable(app_icon_image_test test/app_icon_image_test.c app_icon_image.c)
target_include_directories(app_icon_image_test PRIVATE .)
target_compile_options(app_icon_image_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(UNIX)
    target_link_libraries(app_icon_image_test PRIVATE m)
endif()
add_executable(ico_icon_test test/ico_icon_test.c app_icon_ico.c app_icon_image.c)
target_include_directories(ico_icon_test PRIVATE .)
target_compile_options(ico_icon_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_icon_test PRIVATE ico_host_fs)
add_executable(app_icon_files_test test/app_icon_files_test.c app_icon.c app_icon_ico.c
                                   app_icon_image.c)
target_include_directories(app_icon_files_test PRIVATE .)
target_compile_options(app_icon_files_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(app_icon_files_test PRIVATE ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME app_icon_image COMMAND app_icon_image_test)
    add_test(NAME ico_icon COMMAND ico_icon_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME app_icon_files COMMAND app_icon_files_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The exe's own icon (icon_stamp.h): the RT_GROUP_ICON directory and the
# RT_ICON images built from an .ico the writer made, the stamp text and the
# command-line quoting. The resource update itself (tools/ico_icon_stamp.c,
# Windows only) is checked on Windows by hand (docs/port/APP_ICON.md).
add_executable(icon_stamp_test test/icon_stamp_test.c icon_stamp.c app_icon_ico.c app_icon_image.c)
target_include_directories(icon_stamp_test PRIVATE .)
target_compile_options(icon_stamp_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(icon_stamp_test PRIVATE ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME icon_stamp COMMAND icon_stamp_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
# tools\ico_icon_stamp.exe, shipped by tools/package_win.sh: gives the game's
# exe the captured icon after the game exits (app_icon_exe.c starts it).
# A program with no window (GUI subsystem); kernel32 and shell32 only.
if(WIN32)
    add_executable(ico_icon_stamp "${CMAKE_SOURCE_DIR}/tools/ico_icon_stamp.c" icon_stamp.c)
    target_include_directories(ico_icon_stamp PRIVATE .)
    target_compile_options(ico_icon_stamp PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_options(ico_icon_stamp PRIVATE -mwindows)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_options(ico_icon_stamp PRIVATE -static-libgcc)
    endif()
    target_link_libraries(ico_icon_stamp PRIVATE shell32)
endif()

# Extras > Add to the applications menu (desktop_entry.c): the entry's text
# and the install over a fake data folder under the build folder
# (Linux and the other Unixes: the test sets environment variables with setenv)
if(UNIX)
    add_executable(desktop_entry_test test/desktop_entry_test.c desktop_entry.c)
    target_include_directories(desktop_entry_test PRIVATE .)
    target_compile_options(desktop_entry_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(desktop_entry_test PRIVATE ico_host_fs)
    if(ICO_RUN_TESTS)
        add_test(NAME desktop_entry COMMAND desktop_entry_test "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()

# The bug report's zip (report.c): the stored loss dump deflated into it,
# the slow-scene zip stored, read back with miniz's reader; it writes under
# the build folder
add_executable(report_test test/report_test.c)
target_compile_options(report_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(report_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME report COMMAND report_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# logs/last-run.txt (last_run.c, issue 215): the slots' text and parse, a
# file cut and a write torn at every byte offset, the previous run's lines,
# the phase rules and the summary, and the run's own file through its calls
# (the crash's end, the next start's lines and rename; issue 215's boot id,
# the set's count made and a start ended between its renames); it writes
# under the build folder
add_executable(last_run_test test/last_run_test.c)
target_compile_options(last_run_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(last_run_test PRIVATE ico_platform ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME last_run COMMAND last_run_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The zero-area cull's mark on the install (vu_zero_mark.c, issue 179): the
# end kind from last-run.txt's end and Android's exit record, the stage table
# (trying: every end but a clean close blocks; counting and on: only a crash,
# an ANR, a signal, a device loss, a GPU hang or the watchdog; failed and
# blocked always), every mark with its version (a block holds for the
# version that ran, an update clears it), and the start's step on files
# under the build folder (closed from the recent apps list while counting:
# not blocked; a crash while counting: blocked for that version, not the
# next; the file rewritten, the environment exported, one line)
add_executable(vu_zero_mark_test test/vu_zero_mark_test.c)
target_compile_options(vu_zero_mark_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(vu_zero_mark_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    # a folder of its own: last-run-previous.txt is written by both mark tests
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_zero_mark_test_files")
    add_test(NAME vu_zero_mark COMMAND vu_zero_mark_test "${CMAKE_CURRENT_BINARY_DIR}/vu_zero_mark_test_files")
endif()

# The discard-free twins' mark on the install (nodiscard_mark.c, issue 179):
# the zero-area cull's stages and table with its own file and words, every
# stage at a crash (blocked for that version, the next one tries again), a
# close from the recent apps while counting and on (not blocked), and the
# start's step on files under the build folder (the file rewritten, the
# environment exported, one line; nothing said without a mark)
add_executable(nodiscard_mark_test test/nodiscard_mark_test.c)
target_compile_options(nodiscard_mark_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(nodiscard_mark_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    # a folder of its own: last-run-previous.txt is written by every mark test
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nodiscard_mark_test_files")
    add_test(NAME nodiscard_mark COMMAND nodiscard_mark_test
                                         "${CMAKE_CURRENT_BINARY_DIR}/nodiscard_mark_test_files")
endif()

# The fast pixel build's mark on the install (fastps_mark.c, issue 179):
# the zero-area cull's stages and table with its own file and words, every
# stage at a crash (blocked for that version, the next one tries again), a
# close from the recent apps while counting (not blocked), an end while the
# cull's or the twins' mark said trying (theirs, not blocked here), and the
# start's step on files (the file rewritten, the environment exported, one
# line; nothing said without a mark; the cull's and the twins' marks judge
# an end while it was trying as its own)
add_executable(fastps_mark_test test/fastps_mark_test.c)
target_compile_options(fastps_mark_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(fastps_mark_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    # a folder of its own: last-run-previous.txt is written by every mark test
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fastps_mark_test_files")
    add_test(NAME fastps_mark COMMAND fastps_mark_test
                                      "${CMAKE_CURRENT_BINARY_DIR}/fastps_mark_test_files")
endif()

# The index compaction's mark on the install, for its Mali-G52 form
# (compact_mark.c, issue 179): the zero-area cull's stages and table with its
# own file and words, every stage at a crash (blocked for that version, the
# next one tries again), a device loss while trying, a close from the recent
# apps while counting (not blocked), and the start's step on files, first of
# the marks: the other attempts' marks as the previous run left them (an end
# while the twins, the lean vertex outputs or the rate card were trying is
# theirs), the file rewritten, the environment exported, one line; nothing
# said without a mark
add_executable(compact_mark_test test/compact_mark_test.c)
target_compile_options(compact_mark_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(compact_mark_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    # a folder of its own: last-run-previous.txt is written by every mark test
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/compact_mark_test_files")
    add_test(NAME compact_mark COMMAND compact_mark_test
                                       "${CMAKE_CURRENT_BINARY_DIR}/compact_mark_test_files")
endif()

# The next picture submitted ahead, its mark on the install (ahead_mark.c,
# issue 179): the state table (on after a loss, a hang, the watchdog, a crash
# or Android's crash record: blocked for the version; on after a clean end, no
# end written or an ordinary close by Android's record: cleared; blocked on
# this version: kept, another: cleared), and the start's step on files under
# the build folder (a loss with the order in effect: off for this version,
# kept at the next start, on again in the next version; a close from the
# recent apps: not blocked; the in-effect write once a process)
add_executable(ahead_mark_test test/ahead_mark_test.c)
target_compile_options(ahead_mark_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ahead_mark_test PRIVATE ico_platform ico_host_fs)
if(ICO_RUN_TESTS)
    # a folder of its own: last-run-previous.txt is written by both mark tests
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ahead_mark_test_files")
    add_test(NAME ahead_mark COMMAND ahead_mark_test "${CMAKE_CURRENT_BINARY_DIR}/ahead_mark_test_files")
endif()

# The skin check's counts in words (last_run_skin.c, issue 217): the
# pipelines slot's words and the summary's short form word for word, the
# check off, the largest counts within their bound, a short buffer
add_executable(last_run_skin_test test/last_run_skin_test.c)
target_compile_options(last_run_skin_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(last_run_skin_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME last_run_skin COMMAND last_run_skin_test)
endif()

# Where the device's memory is (mem_breakdown.c, mem_watch.c, issue 215):
# /proc/meminfo, /proc/self/status and the /sys nodes parsed from a fake
# tree under the build folder (short reads, missing fields, unreadable and
# missing files), the parts and their texts word for word, the batch
# lines' every-Nth and fell-by-step rules and their bound, the set's
# summary arithmetic, and last-run.txt's memory field through a round trip
add_executable(mem_breakdown_test test/mem_breakdown_test.c)
target_compile_options(mem_breakdown_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(mem_breakdown_test PRIVATE ico_platform ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME mem_breakdown COMMAND mem_breakdown_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The start-up set sized by the device's own memory fall (set_size.c, issue
# 215): the median, the decision's table (the issue 215 handheld's and the
# issue 123 tablet's figures, no fall under the floor, too few batches, a
# fit, the order of two families, no floor), a whole start of each of the
# two devices with their lines word for word, a desktop without a floor and
# a phone without a fall, a noisy batch, a mixed one, an unread one, and
# every text at its widest within its buffer
add_executable(set_size_test test/set_size_test.c)
target_compile_options(set_size_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(set_size_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME set_size COMMAND set_size_test)
endif()

# How the previous run ended (exit_info.c, issue 215): Android's exit
# records' text parsed (every reason, a trace, the cap, older and malformed
# lines), the reasons' words, the verdict's precedence table and its room,
# the two issue 215 runs, and the start's part through ICO_TEST_EXIT_INFO
# (the log's lines, exit-info.txt, the seen file, the report's verdict); it
# writes under the build folder
add_executable(exit_info_test test/exit_info_test.c)
target_compile_options(exit_info_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(exit_info_test PRIVATE ico_platform ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME exit_info COMMAND exit_info_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# F12's zip (dump_zip.c): the dump and the log in one archive, read back
# with miniz's reader; it writes under the build folder
add_executable(dump_zip_test test/dump_zip_test.c dump_zip.c upscale_pictures.c)
target_compile_options(dump_zip_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(dump_zip_test PRIVATE ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME dump_zip COMMAND dump_zip_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# log_cost_test: the logging's own cost a block (log_cost.h): the window
# thread's print, the log thread's writes and their overlaps with a present
# that blocked and a late step, the pipe full or not, a capture, the line
# word for word (CPU only)
# sys_cell_test: the last-value cell the log thread writes and the 10 s
# lines read (sys_cell.h): read back whole with its time and cost, a write
# in progress read as nothing, a writer thread against a reader (CPU only)
add_executable(sys_cell_test test/sys_cell_test.c sys_cell.c)
target_include_directories(sys_cell_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(sys_cell_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(sys_cell_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME sys_cell COMMAND sys_cell_test)
endif()

# attempt_ledger_test: the attempt ledger's rows and lines (attempt_ledger.h,
# in ico_thread): states, the block's counts taken per block, the stalls
# clause, a short buffer (CPU only)
add_executable(attempt_ledger_test test/attempt_ledger_test.c)
target_include_directories(attempt_ledger_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(attempt_ledger_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(attempt_ledger_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME attempt_ledger COMMAND attempt_ledger_test)
endif()

# core_share_test: the share of run time on the faster cores of the threads
# that do not sample their own core (core_share.h, issue 179): the sums by
# name, the line word for word, field 39 of a thread's stat (CPU only)
add_executable(core_share_test test/core_share_test.c core_share.c)
target_include_directories(core_share_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(core_share_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(core_share_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME core_share COMMAND core_share_test)
endif()

# stall_coincide_test: what a stall coincided with (stall_coincide.h, issue
# 179): the block's pass medians, the spike rule, a moment's classes, the
# tally and moment lines word for word, the pressure and status parsers, the
# driver's thread names, the block's stalls against the pieces' shares and
# every stall's table row (the two timed replays after, a row carried to the
# next block, parts of 12 rows, the rows past 256 counted), the 2 s
# triangles' most (CPU only)
add_executable(stall_coincide_test test/stall_coincide_test.c stall_coincide.c)
target_include_directories(stall_coincide_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(stall_coincide_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME stall_coincide COMMAND stall_coincide_test)
endif()

# power_read_test: the battery for the log (power_read.h, issue 179): the
# sysfs numbers and words, the Java side's four ints, a block's currents,
# voltages, level and status, the once and block lines word for word, from
# strings in the test (CPU only)
add_executable(power_read_test test/power_read_test.c power_read.c)
target_include_directories(power_read_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(power_read_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME power_read COMMAND power_read_test)
endif()

# thermal_read_test: the phone's heat and clocks (thermal_read.h, issue 179)
# over fixture trees it writes in the build folder: the parsers, the zones
# chosen by type (garbage, unreadable, none of the types, two hundred), no
# thermal folder, the CPU clusters from cpufreq's policies and from each
# core's folder, a second's reading, the once, token and block lines word
# for word (CPU only)
# (not on Windows: the test makes a file unreadable with chmod)
if(NOT WIN32)
    add_executable(thermal_read_test test/thermal_read_test.c thermal_read.c)
    target_include_directories(thermal_read_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(thermal_read_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(thermal_read_test PRIVATE ico_host_fs)
    if(ICO_RUN_TESTS)
        add_test(NAME thermal_read COMMAND thermal_read_test "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()

# vmstat_read_test: the kernel's memory events a block (vmstat_read.h, issue
# 179): the counts from a /proc/vmstat text (old and new kernels' names, the
# zones' and the refaults' sums), a count missing, a count gone back, the
# line word for word (CPU only)
add_executable(vmstat_read_test test/vmstat_read_test.c vmstat_read.c)
target_include_directories(vmstat_read_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(vmstat_read_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME vmstat_read COMMAND vmstat_read_test)
endif()

# present_latency_test: the present's latency in a 10 s block
# (present_latency.h, issue 123): the mean and longest from the hand-over or
# the replay's start, the parts before the present and of it, the driver's
# call split at the picture's GPU end, records left out, the line word for
# word (CPU only)
add_executable(present_latency_test test/present_latency_test.c present_latency.c)
target_include_directories(present_latency_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(present_latency_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME present_latency COMMAND present_latency_test)
endif()

add_executable(log_cost_test test/log_cost_test.c log_cost.c)
target_include_directories(log_cost_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(log_cost_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME log_cost COMMAND log_cost_test)
endif()

# perf_hint_test: Android's performance hints, the pure part (perf_hint.h):
# the target from what the pacer plans, a picture's work from its record,
# the session's life against a fake backend (made, reports, target and
# thread changes, the background, refused, a full ring), the lines word for
# word, and the hand-over on two real threads (CPU only)
add_executable(perf_hint_test test/perf_hint_test.c perf_hint.c)
target_include_directories(perf_hint_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(perf_hint_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(perf_hint_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME perf_hint COMMAND perf_hint_test)
endif()

# cpu_keep_test: the render, game and submit threads' cores kept after the
# start (cpu_keep.h): the rule from the cores' top speeds (the owner's
# Pixel, a Helio G85, a 4 + 3 + 1 phone, 4 + 4, a 4-core device, one speed,
# one core), the keeper against a fake kernel (all kept, core 7 alone
# widened to 2-7 as on the Pixel, the system changing the cores, every
# one-core pin widened, a cpuset without core 7, ids not known yet), the
# lines word for word; the pins let go and taken again (the RELEASED sets,
# none under the few-cores rule); the few-cores rule (one or two faster cores) and its
# sweep, each thread's cores on five layouts from a fake sysfs under the
# build folder; cpu_place's samples by core, a fake cpuset under the
# build folder and the registered free threads moved back (Linux)
add_executable(cpu_keep_test test/cpu_keep_test.c cpu_keep.c)
target_include_directories(cpu_keep_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(cpu_keep_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(cpu_keep_test PRIVATE ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME cpu_keep COMMAND cpu_keep_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The automatic dumps' worker (dump_auto.c, issues 123 and 179): a job written
# whole off the calling thread, a second job while one is written dropped
# with its line, the join at shutdown, the zip from memory and the bug
# report's bundle of the run's automatic zips (dump_zip.c), the loss keep's
# block slot, and which replay an armed slow-scene dump takes (slow_arm.c)
add_executable(dump_auto_test test/dump_auto_test.c dump_auto.c dump_zip.c slow_arm.c)
target_include_directories(dump_auto_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(dump_auto_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(dump_auto_test PRIVATE ico_host_fs ico_miniz ico_thread)
if(ICO_RUN_TESTS)
    add_test(NAME dump_auto COMMAND dump_auto_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Options > Extras > Back up your data and Restore from a backup (backup.c):
# a user folder under the build folder backed up, inspected, unpacked and
# put in place, byte for byte, and the refusals (not a backup, a path that
# climbs out, past the bounds, a busy card or save state) and a failed move
# undone
add_executable(backup_test test/backup_test.c backup.c)
target_compile_options(backup_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(backup_test PRIVATE ico_host_fs ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME backup COMMAND backup_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# An rc build's Share a bug report's files (share_report.c, compiled into
# the program only with ICO_RC_TOOLS on Android), over a fake logs and dumps
# folder under the build folder; tested in every build
add_executable(share_report_test test/share_report_test.c share_report.c)
target_compile_options(share_report_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(share_report_test PRIVATE ico_host_fs)
if(ICO_RUN_TESTS)
    add_test(NAME share_report COMMAND share_report_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# An rc build's frames of the opening: when they are taken (capture_trigger.c,
# compiled into the program only with ICO_RC_TOOLS on Android), over made-up
# sequences of glow light plans, frames, times and the opening; tested in
# every build
add_executable(capture_trigger_test test/capture_trigger_test.c capture_trigger.c)
target_compile_options(capture_trigger_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(capture_trigger_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(ICO_RUN_TESTS)
    add_test(NAME capture_trigger COMMAND capture_trigger_test)
endif()

# The rc bench's pure parts (bench_plan.c, compiled into the program only
# with ICO_RC_TOOLS on Android, and into rd_replay_tool): the picks of the
# opening's frames, the schedule, the statistics, the lines and the CSV,
# over made-up records and samples; tested in every build
add_executable(bench_plan_test test/bench_plan_test.c bench_plan.c)
target_compile_options(bench_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(bench_plan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX)
    target_link_libraries(bench_plan_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME bench_plan COMMAND bench_plan_test)
endif()

# game_data_range: the game data ranges (game_data_range.c) over a stand-in
# game library that gets ico_game's section treatment
# (cmake/IcoStateSections.cmake) and the game's semantics options: the
# ranges hold its writable globals and nothing of the program's, the renamed
# sections keep their contents and relocations, and a copy of the ranges
# written back restores them
add_library(game_data_range_fixture STATIC EXCLUDE_FROM_ALL test/game_data_fixture.c)
target_compile_options(game_data_range_fixture PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_GAME_LAYOUT_OPTIONS})
ico_state_sections(game_data_range_fixture)
add_executable(game_data_range_test test/game_data_range_test.c game_data_range.c)
target_include_directories(game_data_range_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(game_data_range_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(game_data_range_test PRIVATE game_data_range_fixture)
if(ICO_RUN_TESTS)
    add_test(NAME game_data_range COMMAND game_data_range_test)
endif()

# state_format: the save state file (state_file.c) written on its thread and
# read back, every header refusal, bad and truncated files, the build id; it
# writes under the build folder
add_executable(state_format_test test/state_format_test.c)
target_compile_options(state_format_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(state_format_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME state_format COMMAND state_format_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# host_fs.h on a non-ASCII (UTF-8) path in the build tree
add_executable(host_fs_test test/host_fs_test.c)
target_compile_options(host_fs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(host_fs_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME host_fs COMMAND host_fs_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(ICO_RUN_TESTS AND NOT WIN32)
    # a stack overflow in a fiber must hit the guard page
    add_test(NAME fiber_guard COMMAND fiber_test guard)
    set_tests_properties(fiber_guard PROPERTIES SKIP_RETURN_CODE 77)
    # and in a fiber restored from a save state into another pool
    add_test(NAME state_fiber_guard COMMAND state_fiber_test guard)
    set_tests_properties(state_fiber_guard PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The game's allocator (fumi/ios/memory.c) over the arena, compiled as the
# game compiles it; the test checks the partition layout for the host's
# record sizes.
add_executable(memory_test test/memory_test.c "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
ico_include_dirs(fumi _ico_fumi_incs)
target_include_directories(memory_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(memory_test PRIVATE ${ICO_GAME_DEFINITIONS} ICO_HEAP_STATS=1)
target_compile_options(memory_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(memory_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME memory COMMAND memory_test)
endif()

# Issue 151: the game's inflate (fumi/ios/inflate.c) over its allocator and
# its code-table blocks (memory.c, mblock.c), compiled as the game compiles
# them: fixed-code deflate blocks decode and their tables are freed at the
# close without an IOSFREE() assert, and a bad free's assert names the block's
# allocation.  `synth` runs everywhere on streams the test holds; the disc
# runs inflate the packs that hold fixed-code blocks (the Asian disc's
# STGST13C.DF, stage 4, and STGST02A.DF; the Korean disc's STGSTAFF1.DF; the
# August 2001 preview's STGST13C.DF) to their end and are skipped (77) without
# the image.
add_executable(inflate_test test/inflate_test.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/inflate.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/mblock.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
target_include_directories(inflate_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(inflate_test PRIVATE ${ICO_GAME_DEFINITIONS} ICO_HEAP_STATS=1)
target_compile_options(inflate_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(inflate_test PRIVATE ico_port_data ico_platform)
# The test frees a pointer 8 bytes into a block on purpose (the EE walk's
# mistake on the host); iosFree reads the 16-byte-aligned header in front of
# it, which UBSan's alignment check reports, and its assert message then
# reads the shifted node's neighbours: the shifted `next` lands on the block's
# free_prev, NULL for a block in use, so the message's "%s" of that node's
# tag (at offset 0) is a NULL member access UBSan reports (glibc prints
# "(null)").  Only this target drops those two checks.
if(ICO_SANITIZE)
    target_compile_options(inflate_test PRIVATE $<$<COMPILE_LANGUAGE:C>:-fno-sanitize=alignment,null>)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME inflate COMMAND inflate_test synth)
    add_test(NAME inflate_disc_cn COMMAND inflate_test disc "${ICO_DISC_IMAGE_CN}" STGST13C.DF
                                          9831100 483)
    add_test(NAME inflate_disc_cn_st02a COMMAND inflate_test disc "${ICO_DISC_IMAGE_CN}"
                                                STGST02A.DF 9486676 497)
    add_test(NAME inflate_disc_kor COMMAND inflate_test disc "${ICO_DISC_IMAGE_KOR}"
                                           STGSTAFF1.DF 7154080 232)
    add_test(NAME inflate_disc_aug6 COMMAND inflate_test disc "${ICO_DISC_IMAGE_AUG6}"
                                            STGST13C.DF 9656788 461)
    set_tests_properties(inflate_disc_cn inflate_disc_cn_st02a inflate_disc_kor inflate_disc_aug6
                         PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Save states' relocation (state_reloc.c, state_check.c): the scan over
# synthetic regions with pointers into each moved range, near misses,
# floats and EE word pairs; the program's image range; the game heap's
# typed walk over a real partition (fumi/ios/memory.c, compiled as the game
# compiles it) copied to another address and relocated; the thread and
# handler checks; the masked hash of the game's state (state_hash.c): typed
# fields and words into noted host blocks left out, everything else counted.
add_executable(state_reloc_test test/state_reloc_test.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
target_include_directories(state_reloc_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(state_reloc_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(state_reloc_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(state_reloc_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME state_reloc COMMAND state_reloc_test)
endif()

# The game's thread, message and memory layers on the scheduler, driven by
# simulated vsyncs.
add_executable(ios_chain_test test/ios_chain_test.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
target_include_directories(ios_chain_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(ios_chain_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(ios_chain_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(ios_chain_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME ios_chain COMMAND ios_chain_test)
endif()

# The pad as a movie's skip poll sees it (main.c movie_abort_check): the game's
# keyInput.c, pad.c and shockdriver.c over the thread and message layers and
# port/input's libpad and bindings, on the scheduler, a thread spinning to the
# vsync and polling, fed a gamepad Start and a key bound to Start.
add_executable(movie_pad_test test/movie_pad_test.c host_config.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/pad.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/shockdriver.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/keyInput.c")
target_include_directories(movie_pad_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(movie_pad_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(movie_pad_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(movie_pad_test PRIVATE ico_port_null ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME movie_pad COMMAND movie_pad_test)
endif()

# Issue 144: Mirror mode and the d-pad.  The game's iosPadGetStick (pad.c) over
# a centred stick and the d-pad bits, mirror off and on, both disc copies: the
# synthesised x flips with the option, y and a deflected stick do not.
add_executable(pad_mirror_test test/pad_mirror_test.c host_config.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/pad.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/shockdriver.c")
target_include_directories(pad_mirror_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(pad_mirror_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(pad_mirror_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(pad_mirror_test PRIVATE ico_port_null ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME pad_mirror COMMAND pad_mirror_test)
endif()

# The window mode requests' readback (window_video.c) on SDL's
# offscreen video driver. SDL3 is found by port/rhi, configured after this
# directory, so the test is defined once the top level is done; exit 77
# (skipped) without the offscreen driver.
function(_ico_window_video_test)
    if(NOT TARGET SDL3::SDL3-shared)
        return()
    endif()
    set(_dir "${CMAKE_SOURCE_DIR}/port/platform")
    add_executable(window_video_test "${_dir}/test/window_video_test.c" "${_dir}/window_video.c")
    target_compile_options(window_video_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(window_video_test PRIVATE SDL3::SDL3-shared)
    ico_copy_sdl3_dll(window_video_test)
    if(ICO_RUN_TESTS)
        add_test(NAME window_video COMMAND window_video_test)
        set_tests_properties(window_video PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_window_video_test)

# backend_switch_window: the window build's Options > Graphics > Apply,
# Vulkan (lavapipe) -> OpenGL (llvmpipe) -> Vulkan through
# ico_window_switch_backend and ico_window_reopen, on SDL's offscreen
# driver, driven by [dev] backend_switch_test (test/backend_switch.cmake);
# Linux, both backends built, the disc image (skipped without it). A game
# run with two renderer starts on software drivers: alone, label window.
# Deferred: port/rhi (the OpenGL backend's target) is configured after this
# directory.
function(_ico_backend_switch_test)
    if(NOT TARGET ico_pc OR ICO_HEADLESS OR NOT ICO_RUN_TESTS OR NOT TARGET ico_rhi_opengl
       OR NOT TARGET ico_rhi_vulkan OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
        return()
    endif()
    set(_dir "${CMAKE_SOURCE_DIR}/port/platform")
    add_test(NAME backend_switch_window
             COMMAND "${CMAKE_COMMAND}" "-DICO_PC=$<TARGET_FILE:ico_pc>" "-DISO=${ICO_DISC_IMAGE}"
                     "-DDIR=${CMAKE_BINARY_DIR}/port/platform/backend_switch_run"
                     "-DLVP=/usr/share/vulkan/icd.d/lvp_icd.json"
                     -P "${_dir}/test/backend_switch.cmake")
    set_tests_properties(backend_switch_window PROPERTIES RUN_SERIAL TRUE TIMEOUT 1500
                         SKIP_REGULAR_EXPRESSION "backend_switch_window: skipped"
                         LABELS "platform;window")
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_backend_switch_test)

# what slow pictures and steps are judged against (slow_judge.c, issue 179)
include(slow_judge.cmake)

# the save states' carry-over guard: state_carry_probe and its tests
# (state_carry_check, state_carry_image, state_carry_xbuild; issue 217)
include(state_carry.cmake)
