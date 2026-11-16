# rd_inames.cmake: the draws left at their tick's position, by name, and the
# stair-steppers (rd_inames.c the tally, the stair test and the origin;
# rd_inames_live.c rd_interp.c's side; rd_inames.h; the owner's Pixel in the
# cage room, v0.11.7), included at the end of port/render/CMakeLists.txt (a
# file of its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_inames.c rd_inames_live.c)

# rd_inames_test: CPU only: the tally by name and reason (a name of 24
# bytes and more cut, the table full counted), the top five of each reason
# and the line word for word (an unmatched name's texture scroll beside
# it), a short buffer; the stair test's verdicts (an
# object moved every other tick steps at each still tick after a move and
# alternates from its fourth judged tick, one moved every tick never, one
# that stops once, a key drawn twice in a tick judged once, a tick skipped
# not judged, the table emptied when three quarters full), the cadence by
# name and its window; the tick before's rows carried to this tick's view
# (the camera's motion cancels, the 2^-17 bound at its edge, the GS Z lane
# left out, a view that does not invert, the kept carry)
add_executable(rd_inames_test test/rd_inames_test.c rd_inames.c)
target_include_directories(rd_inames_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_inames_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_inames_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_inames COMMAND rd_inames_test)
endif()
