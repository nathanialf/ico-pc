# cmake/IcoExclusions.cmake: non-renderer game sources the host build cannot
# compile yet, left out so the rest builds. docs/port/BUILD_STATUS.md has the
# details, the counts and the owners. ICO_BUILD_BLOCKED=ON compiles them
# anyway, to recheck after a fix lands; delete an entry once it compiles.
#
# Reasons:
#   vu0-asm     R5900/VU0 inline assembly: none left in the non-renderer
#               sources (package 1A rewrote it in C over port/math/,
#               docs/port/MATH.md); typedef.h makes any new use a compile
#               error on the host.
#   nested-fn   GNU C nested functions, which clang does not implement (gcc
#               does): package 0B, or the toolchain decision (BUILD_STATUS.md).
#   decl        a declaration conflicting with an earlier implicit one
#               (clang makes it an error): package 0B.

# Fail with every compiler.
set(ICO_BLOCKED_SOURCES
)

# Fail with clang only (the default presets); the *-gcc presets compile them.
set(ICO_BLOCKED_SOURCES_CLANG
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND ICO_BLOCKED_SOURCES ${ICO_BLOCKED_SOURCES_CLANG})
endif()
