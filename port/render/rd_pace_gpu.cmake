# rd_pace_gpu.cmake: each in-between picture drawn for the moment its GPU
# work is predicted to end (rd_pace_gpu.c, rd_pace_gpu_live.c, rd_pace_gpu.h:
# the GPU queue's model from the measured spans, first submits and ends, the
# evenness line's last clause, the attempt ledger's row), included from
# rd_even.cmake (port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_pace_gpu.c rd_pace_gpu_live.c)

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
# and the evenness clause word for word)
add_executable(rd_pace_gpu_test test/rd_pace_gpu_test.c rd_pace_gpu.c rd_pace_gpu_live.c
                                rd_rebase.c)
target_include_directories(rd_pace_gpu_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_pace_gpu_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_pace_gpu_test PRIVATE ico_thread)
if(NOT WIN32)
    target_link_libraries(rd_pace_gpu_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_pace_gpu COMMAND rd_pace_gpu_test)
endif()
