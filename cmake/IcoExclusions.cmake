# cmake/IcoExclusions.cmake: non-renderer game sources the host build cannot
# compile yet, left out so the rest builds. docs/port/BUILD_STATUS.md has the
# details, the counts and the owners. ICO_BUILD_BLOCKED=ON compiles them
# anyway, to recheck after a fix lands; delete an entry once it compiles.
#
# Reasons:
#   vu0-asm     R5900/VU0 inline assembly (lqc2, vmul, qmfc2, mtc1 with an
#               "=f" output): package 1A (VU0 transpiler, port/math/).
#               sugiCommon.h's plane_distance, distance_squared,
#               distance_squared_b and distance_squared_xz are static inline
#               functions in a header: clang rejects their asm in every TU
#               that includes it, gcc only in TUs that call them.
#   break-asm   fumi/ios/memory.c only: `__asm__("break")`, the EE debug trap
#               (the others use ICO_BREAK() from typedef.h): package 1B.
#   nested-fn   GNU C nested functions, which clang does not implement (gcc
#               does): package 0B, or the toolchain decision (BUILD_STATUS.md).
#   decl        a declaration conflicting with an earlier implicit one
#               (clang makes it an error): package 0B.

# Fail with every compiler.
set(ICO_BLOCKED_SOURCES
    ico2/fumi/ios/memory.c                  # break-asm
    ico2/fumi/src/commonact.c               # vu0-asm (sugiCommon.h)
    ico2/fumi/src/fieldCollision.c          # vu0-asm (sugiCommon.h)
    ico2/ito/src/itou_sub.c                 # vu0-asm
    ico2/seki/src/BgAnimation.c             # vu0-asm
    ico2/sugipon/src/a_p_1.c                # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/act_a_p_1.c            # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/box.c                  # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/enemy.c                # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/geometryManager.c      # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/girlForceField.c       # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/item.c                 # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/motionManager.c        # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/motionManager2.c       # vu0-asm (sugiCommon.h and its own); nested-fn
    ico2/sugipon/src/spider.c               # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/stormTest.c            # vu0-asm
    ico2/sugipon/src/torch.c                # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/weapon.c               # vu0-asm (sugiCommon.h)
    ico2/sugipon/src/windField.c            # vu0-asm (sugiCommon.h)
)

# Fail with clang only (the default presets); the *-gcc presets compile them.
set(ICO_BLOCKED_SOURCES_CLANG
    ico2/fumi/src/enemy_act.c               # vu0-asm (sugiCommon.h, unused)
    ico2/ito/src/act_bird.c                 # vu0-asm (sugiCommon.h, unused)
    ico2/ito/src/itou_boss.c                # vu0-asm (sugiCommon.h, unused)
    ico2/omori/src/enemy-control.c          # vu0-asm (sugiCommon.h, unused)
    ico2/script/src/script.c                # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/boy.c                  # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/enemyParts.c           # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/frameDependSequence.c  # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/motionViewer.c         # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/pool.c                 # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/rope.c                 # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/waterDot.c             # vu0-asm (sugiCommon.h, unused)
    ico2/sugipon/src/windManager.c          # vu0-asm (sugiCommon.h, unused)
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND ICO_BLOCKED_SOURCES ${ICO_BLOCKED_SOURCES_CLANG})
endif()
