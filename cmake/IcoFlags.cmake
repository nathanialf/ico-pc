# cmake/IcoFlags.cmake: the compile options of the game code (ico2/ and the
# generated data tables) on the host. port/ code takes the same semantics
# options and the normal warning set.

# Semantics the game was written against (ee-gcc 2.9, MIPS R5900):
#   -std=gnu11              GNU extensions (statement expressions, typeof,
#                           zero-length arrays) the reconstruction uses.
#   -fno-strict-aliasing    the game reads records through other types.
#   -fwrapv                 signed overflow wraps, as the EE's integer unit.
#   -ffp-contract=off       no fused multiply-add: the EE rounds each step.
#   -fno-fast-math          IEEE ordering; the EE semantics are added by
#                           port/math, never by the compiler.
#   -fsigned-char           ee-gcc's MIPS default is signed char. Package 0C
#                           verifies this against the period compiler's
#                           specs (docs/research/); until then it is assumed.
#   -fno-common             the game was built -fno-common.
#   -fgnu89-inline          ee-gcc 2.9's `inline`: a plain `inline` function
#                           definition is also the external one (script.h
#                           declares scpFadeIn and others `inline` and other
#                           TUs call them). C99 semantics would leave them
#                           undefined at link (package 0C counts 1,253
#                           non-static inline definitions).
set(ICO_SEMANTIC_OPTIONS
    -fno-strict-aliasing
    -fwrapv
    -ffp-contract=off
    -fno-fast-math
    -fsigned-char
    -fno-common
    -fgnu89-inline
)

# Record layout of the game and data TUs only (package 0C, docs/research/).
# port/ code keeps the platform ABI, since SDL's and the Windows SDK's
# structs assume it. A record the game and port/ both read must not depend
# on either option.
#   -mno-ms-bitfields  Windows: bit-fields by the GCC rules the EE used, not
#                      the MS rules mingw targets default to.
set(ICO_GAME_LAYOUT_OPTIONS "")
if(WIN32)
    list(APPEND ICO_GAME_LAYOUT_OPTIONS -mno-ms-bitfields)
endif()

# The three diagnostics the plan makes errors (plan "Verification: Static").
# ICO_STRICT_WARNINGS=ON turns them into errors; it stays OFF until the tree
# compiles clean with it (the build logs count the warnings).
option(ICO_STRICT_WARNINGS "Treat -Wreturn-type, -Wimplicit-function-declaration and -Wstrict-prototypes as errors" OFF)

set(ICO_GAME_WARNINGS -Wreturn-type -Wimplicit-function-declaration -Wstrict-prototypes)
if(ICO_STRICT_WARNINGS)
    list(APPEND ICO_GAME_WARNINGS
        -Werror=return-type
        -Werror=implicit-function-declaration
        -Werror=strict-prototypes)
else()
    # Modern compilers make several C89-era diagnostics errors by default.
    # Until package 0B's front-end pass lands, they stay warnings so every
    # file that can compile does, and the logs count them.
    # Each flag only when this compiler knows it (CheckCCompilerFlag): gcc
    # 13 (the ubuntu-24.04 runner's default) has no -Wreturn-mismatch or
    # -Wdeclaration-missing-parameter-type and rejects -Wno-error= of them.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(_ico_relaxed
            -Wno-error=implicit-function-declaration
            -Wno-error=implicit-int
            -Wno-error=int-conversion
            -Wno-error=incompatible-pointer-types
            -Wno-error=incompatible-function-pointer-types
            -Wno-error=return-type
            -Wno-error=return-mismatch)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(_ico_relaxed
            -Wno-error=implicit-function-declaration
            -Wno-error=implicit-int
            -Wno-error=int-conversion
            -Wno-error=incompatible-pointer-types
            -Wno-error=return-mismatch
            -Wno-error=declaration-missing-parameter-type)
    else()
        set(_ico_relaxed "")
    endif()
    include(CheckCCompilerFlag)
    foreach(_f IN LISTS _ico_relaxed)
        string(MAKE_C_IDENTIFIER "ICO_HAS${_f}" _var)
        check_c_compiler_flag("${_f}" ${_var})
        if(${_var})
            list(APPEND ICO_GAME_WARNINGS ${_f})
        endif()
    endforeach()
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Older clangs do not know every name above. The EUC-JP string literals
    # (.gitattributes) are not UTF-8; clang keeps their bytes as written and
    # only warns, once per literal.
    list(APPEND ICO_GAME_WARNINGS -Wno-unknown-warning-option -Wno-invalid-source-encoding)
endif()

set(ICO_PORT_WARNINGS -Wall -Wextra -Wno-unused-parameter -Werror
    -Werror=return-type -Werror=implicit-function-declaration -Werror=strict-prototypes)

# Sanitizers (the asan preset): ICO_SANITIZE=address,undefined.
set(ICO_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list, empty for none")
if(ICO_SANITIZE)
    add_compile_options(-fsanitize=${ICO_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=${ICO_SANITIZE})
endif()
