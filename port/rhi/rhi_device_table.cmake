# rhi_device_table.cmake: the device table (rhi_device_table.c/.h; the hardware
# rows that set renderer-internal choices, docs/port/DEVICE_TABLE.md),
# included from port/rhi/CMakeLists.txt beside the pass breadcrumb's library.
# A library of its own, pure C (no Vulkan, SDL, I/O or environment): every
# backend, the renderer and the platform may read it.  ico_rhi and every
# backend library link it PUBLIC (port/rhi/CMakeLists.txt).
add_library(ico_device_table STATIC rhi_device_table.c)
target_include_directories(ico_device_table PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_device_table PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# The pure predicates that read the table and keep their signatures
# (rhi_pass_row.c, rhi_driver_version.c, rhi_tile_rule.c): their tests,
# registered in port/rhi/CMakeLists.txt before this file, link it too
target_link_libraries(rhi_driver_version_test PRIVATE ico_device_table)
target_link_libraries(rhi_vk_pass_row_test PRIVATE ico_device_table)
target_link_libraries(rhi_vk_qprio_test PRIVATE ico_device_table)
target_link_libraries(rhi_vk_tile_rule_test PRIVATE ico_device_table)

# rhi_device_table_test (CPU only, every target): unique ids, every setting's
# issue, evidence, since and revisit, the derived facts read before what a
# row sets, the override links, the start-up lines and the summary's clause
# of five devices word for word.  With --check-docs it compares the row list
# in docs/port/DEVICE_TABLE.md (between its markers) with the table:
#   rhi_device_table       the checks
#   rhi_device_table_docs  the docs' list (regenerate: rhi_device_table_test --docs)
add_executable(rhi_device_table_test test/rhi_device_table_test.c)
target_compile_options(rhi_device_table_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rhi_device_table_test PRIVATE ico_device_table)

# rhi_device_table_equiv_test (CPU only, every target): the table changes no
# behaviour.  Static copies of every rule it replaces (test/
# rhi_device_table_ref.inc) against the table over a matrix of about 1.4
# million identities (every logged device, the edges of each rule's bounds,
# every backend and OS) and the tests' environment fakes on the logged
# devices; the pure files the copies come from are linked in and compared
# with their copies.  About 6 s on the build host at -O2 (more under the
# sanitizers: the timeout).
add_executable(rhi_device_table_equiv_test
               test/rhi_device_table_equiv_test.c rhi_pass_row.c rhi_driver_version.c
               rhi_tile_rule.c rhi_qprio.c "${CMAKE_SOURCE_DIR}/port/platform/memory_floor.c"
               "${CMAKE_SOURCE_DIR}/port/platform/present_fallback.c")
target_include_directories(rhi_device_table_equiv_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(rhi_device_table_equiv_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
target_link_libraries(rhi_device_table_equiv_test PRIVATE ico_device_table)

if(ICO_RUN_TESTS)
    add_test(NAME rhi_device_table COMMAND rhi_device_table_test)
    add_test(NAME rhi_device_table_docs
             COMMAND rhi_device_table_test --check-docs "${CMAKE_SOURCE_DIR}/docs/port/DEVICE_TABLE.md")
    add_test(NAME rhi_device_table_equiv COMMAND rhi_device_table_equiv_test)
    set_tests_properties(rhi_device_table_equiv PROPERTIES TIMEOUT 1200)
endif()
