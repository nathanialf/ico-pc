# rd_ledger.cmake: the scene ledger, what each stage contained and what was
# left out of its pictures by model and reason (rd_ledger.c the tally, the text
# and the kept stages; rd_ledger_live.c the renderer's hooks; rd_ledger.h,
# rd_ledger_live.h; docs/port/SCENE_LEDGER.md; issue 221), included at the end
# of port/render/CMakeLists.txt (a file of its own: that file is near the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_ledger.c rd_ledger_live.c)

# rd_ledger_test: CPU only, synthetic scenes: a model left out by each reason
# in the text with that reason, its ticks or pictures and its count; built and
# never drawn said so, and carried into the next stage; a drawn row read back;
# the replay's half added; the table full; the effects line; the text's
# bounds; the kept stages oldest first; a draw's tally against a control loop
add_executable(rd_ledger_test test/rd_ledger_test.c rd_ledger.c)
target_include_directories(rd_ledger_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_ledger_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_ledger COMMAND rd_ledger_test)
endif()

# drop_ledger_gate (tools/check_drop_ledger.py): every place that leaves a
# model's draw out names a ledger reason: the reasons and their words match,
# every reason has a hook call, the ledgered functions' early returns each
# follow a hook call, the counted drop sites (the box cull's tests, the
# packet box test, the draw filter's callers, the visibility test's skip)
# keep their counts; drop_ledger_gate_selftest: the rule finds an unledgered
# return in a synthetic function and passes a ledgered one
if(ICO_RUN_TESTS AND Python3_FOUND)
    add_test(NAME drop_ledger_gate
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_drop_ledger.py"
                     "${CMAKE_SOURCE_DIR}")
    add_test(NAME drop_ledger_gate_selftest
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_drop_ledger.py"
                     --selftest)
endif()
