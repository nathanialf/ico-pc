# rd_present_scene.cmake: the present from SCENE (rd_present_scene.c,
# rd_present_scene.h; docs/port/RENDER_POST.md "The present from SCENE (off by
# default until measured)"), included from port/render/CMakeLists.txt after
# ico_render and rd_replay_tool (a file of its own: port/render/CMakeLists.txt
# is at the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_present_scene.c)
