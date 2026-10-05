# cmake/IcoExclusions.cmake: game sources the host build cannot compile, left
# out so the rest builds. Both lists are empty: every game source compiles
# with gcc and clang (docs/BUILDING.md, "Compilers"). ICO_BUILD_BLOCKED=ON
# compiles an entry anyway, to recheck after a fix lands; delete an entry
# once it compiles.
#
# Reasons:
#   vu0-asm     R5900/VU0 inline assembly: none left in the non-renderer
#               sources (package 1A rewrote it in C over port/math/,
#               docs/port/MATH.md); typedef.h makes any new use a compile
#               error on the host.
#   nested-fn   GNU C nested functions, which clang does not implement (gcc
#               does); the game's were rewritten as file-scope functions.
#   decl        a declaration conflicting with an earlier implicit one
#               (clang makes it an error): package 0B.

# Fail with every compiler.
set(ICO_BLOCKED_SOURCES
)

# Fail with clang only (the *-clang presets); the gcc presets compile them.
set(ICO_BLOCKED_SOURCES_CLANG
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND ICO_BLOCKED_SOURCES ${ICO_BLOCKED_SOURCES_CLANG})
endif()
