/* libsndn2.a(sound.o).  The source moved to port/audio/sg/sound.c (Phase 4B),
 * which the host build compiles with its ICO_HOST seams; this file keeps the
 * member at its link-order path (config/link_order.pal.txt) and its SDK
 * archive flags (tools/compile_c.sh, tools/gen_ninja.py) for the PS2 build,
 * which compiles the same text with ICO_HOST undefined. */
#include "../../port/audio/sg/sound.c"
