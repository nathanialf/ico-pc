# rd_diag_notes.cmake: the display line once more at the frame width's first
# change and the upload ring's grow words (rd_diag_notes.c, rd_diag_notes.h;
# issue 179), included at the end of port/render/CMakeLists.txt (a file of its
# own: port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_diag_notes.c)

# rd_diag_notes_test: CPU only: the width's first change alone written once
# more with its words, later ones not, a change with another reason without
# them; the ring's words with none before, never used, used (its age, last use
# and need), a new ring's note, short buffers
add_executable(rd_diag_notes_test test/rd_diag_notes_test.c rd_diag_notes.c)
target_include_directories(rd_diag_notes_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_diag_notes_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_diag_notes COMMAND rd_diag_notes_test)
endif()
