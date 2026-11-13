# rd_pace_gpu.cmake: each in-between picture drawn for the moment its GPU
# work is predicted to end (rd_pace_gpu.c, rd_pace_gpu_live.c, rd_pace_gpu.h:
# the GPU queue's model from the measured spans, first submits and ends, the
# evenness line's last clause, the attempt ledger's row), included from
# rd_even.cmake (port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_pace_gpu.c rd_pace_gpu_live.c rd_pace_rules.c
                                  rd_as_made_timer.c rd_glass.c rd_glass_live.c rd_pace_screen.c
                                  rd_pace_screen_live.c)

# rd_pace_gpu_test: CPU only: the model's edges (before its measurements,
# an empty queue and one picture a tick at 0 to the bit, the queue's wait less
# its mean, the bound from the measured spread, the ratio hold both ways, the
# re-anchoring, the restarts); 10 s simulations of the GPU queue per regime
# (the cage at two a tick, the brief's cage pattern, stages 3 and 5 at two to
# three, three a tick, a GPU-bound one to two, one a tick, an idle GPU) with
# the hand-over's moment and the model's through rd_rebase_raw: the content's
# steps, against the GPU's ends, never back, no repeat, the mean moment within
# its derived tolerance, the same pictures; the rc1 log's three runs; the
# renderer's side (the measurements' ring, the ledger's row, the start line
# and the evenness clause word for word); off by default: presentRebase's
# path with the model and the timer computed gives v0.11.5's value to the bit
add_executable(rd_pace_gpu_test test/rd_pace_gpu_test.c rd_pace_gpu.c rd_pace_gpu_live.c
                                rd_rebase.c rd_pace_rules.c rd_as_made_timer.c rd_pace_screen.c
                                rd_pace_screen_live.c)
target_include_directories(rd_pace_gpu_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_pace_gpu_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pace_gpu_test PRIVATE ico_thread)
if(NOT WIN32)
    target_link_libraries(rd_pace_gpu_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_pace_gpu COMMAND rd_pace_gpu_test)
endif()

# rd_as_made_timer_test: CPU only: the own timer's edges (its first picture on
# the hand-over rule's moment, a steady interval stepped exactly, the pull, a
# held picture's slot taken whole, an error over half a tick, the minimum
# step, the restarts, the switch); 10 s simulations with a 120 Hz mailbox
# display (each picture at the first refresh at or after its GPU end, one
# replaced by the next at the same refresh) of the cage, the brief's cage
# pattern, stages 3 and 5, three a tick, the GPU-bound opening, one a tick,
# a change from two to three a tick and back, a 200 ms stall: the three rules
# over the same pictures (the hand-over through rd_rebase_raw, the GPU end
# through rd_pace_gpu.c, the timer), every step, clamps, the largest step and
# the speed against the hand-overs, the GPU's ends and the display; the
# timer never back, no repeat, no clamp, held in its frame's tick at most 20
# times in 10 s, within half a tick of the hand-over rule; the rc1 log's runs
add_executable(rd_as_made_timer_test test/rd_as_made_timer_test.c rd_as_made_timer.c
                                     rd_pace_gpu.c rd_rebase.c)
target_include_directories(rd_as_made_timer_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_as_made_timer_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_as_made_timer_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_as_made_timer COMMAND rd_as_made_timer_test)
endif()

# rd_glass_test: CPU only: the content's speed on the glass (rd_glass.c): a
# pair per rule against the display's gap, its class, against the hand-overs
# and the GPU's ends (exact at both only), the shares; not steps (a frame
# back or on by more than 8, a gap not positive or over a second); the join
# of pictures and display times by id in either order with a present never
# shown; the line word for word; the screen's rule as a fifth measure, its
# prediction scored at the join (on it, one late, one early, further) and
# sent to its model's ring, the display's own figures (presentMargin,
# earliestPresentTime)
add_executable(rd_glass_test test/rd_glass_test.c rd_glass.c rd_pace_screen.c)
target_include_directories(rd_glass_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_glass_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_glass_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_glass COMMAND rd_glass_test)
endif()

# rd_pace_screen_test: CPU only: the screen's refresh (rd_pace_screen.c,
# rd_pace_screen_live.c): the expected time on the glass, the latch's and
# the grid's fits (the period measured, not the one reported; off one grid
# refused), the score; the placement (the chain's first picture at the value
# drawn otherwise to the bit, the lag's step bounded, never back, the holds,
# a restart, every fall-back to the bit); 10 s simulations of v0.11.6-rc4's
# regimes on the owner's phone (play at one and two refreshes, at two levels
# of the prediction's error, the opening at two and three, kept frames, one
# a tick, an exact prediction) with a display model (a 120 Hz grid reported
# as 8.35 ms, a latch lead with a jitter, the clocks' offset, mailbox, the
# times back three refreshes late): the content's speed on the glass for the
# hand-over, the GPU end, the own timer and the screen over the same
# pictures, the score, the holds, the content's rate over the glass; the
# fall-backs over a run (no display times, times that stop, a display at
# 90 Hz); the start line, the clause and the ledger's row
add_executable(rd_pace_screen_test test/rd_pace_screen_test.c rd_pace_screen.c
                                   rd_pace_screen_live.c rd_pace_gpu.c rd_pace_gpu_live.c
                                   rd_rebase.c rd_as_made_timer.c)
target_include_directories(rd_pace_screen_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                        "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_pace_screen_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pace_screen_test PRIVATE ico_thread)
if(NOT WIN32)
    target_link_libraries(rd_pace_screen_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_pace_screen COMMAND rd_pace_screen_test)
endif()
