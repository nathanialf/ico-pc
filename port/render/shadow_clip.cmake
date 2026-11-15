# shadow_clip.cmake: the box the shadow volumes' edges are clipped to, by disc
# (port/include/ico_shadow_clip.h, read by ico2/seki/src/GsBase.c
# gsb_HostShadowClipX / gsb_HostShadowClipY for Shadow.c clipVolumeEdge),
# included at the end of port/render/CMakeLists.txt (a file of its own:
# port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).

# shadow_clip_test: CPU only, no game code: each disc's own box at the 4:3
# frame (512 by 448 or 512 on the European, Japanese, Korean and Asian discs,
# 768 by 672 or 768 on the North American disc and the two 2001 previews), and
# at every native width the box never inside the disc's own, never inside the
# frame's width (the box of every disc before), past the picture's side, and a
# clipped end inside Shadow.c's range test; the widths of 16:9, 32:9 and 48:9;
# a number that is no row takes the European disc's box.  gsb_cull_test checks
# GsBase.c's own two functions against the header under each disc.
add_executable(shadow_clip_test test/shadow_clip_test.c)
target_compile_options(shadow_clip_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME shadow_clip COMMAND shadow_clip_test)
endif()
