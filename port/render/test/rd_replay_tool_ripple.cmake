# rd_replay_tool_ripple*, rd_replay_tool_mirror_floor: a dump replayed by
# rd_replay_tool at the given options, its log and its picture checked
# (issue 106: with Full pixel on, the puddle's ripples drew as bright white
# segments; rd_replay.c drawWrap, rd_water.c rd__block_keeps_repeat).
# -DTOOL=<rd_replay_tool> -DDUMP=<dump> -DOUT=<png> -DARGS=<a|b|...>
# -DEXPECT=<regex>, -DEXPECT2=<regex> (optional): each must match the
#   tool's output at least once (ARGS should carry --list: the water line)
# -DPYTHON=<python3> (optional, with Pillow; without either the picture
#   checks are skipped with a message):
#   -DBOX=x0,y0,x1,y1 -DMAXBRIGHT=<n>: the pixels of the box (given on a
#     1920 x 1080 picture, scaled to OUT's size) whose three channels are
#     all over 200 number at most n (the ring area of the reporter's shot)
#   -DSHA=<hex>: the SHA-256 of OUT's RGB pixels; checked only on lavapipe
#     (the tool's device line names llvmpipe), where it was taken
if(NOT EXISTS "${DUMP}")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: ${DUMP}: no dump (build-host/tmp/corpus is not in the tree)")
  return()
endif()
if(ARGS)
  string(REPLACE "|" ";" ARGS "${ARGS}")
endif()
file(REMOVE "${OUT}")
execute_process(COMMAND "${TOOL}" "${DUMP}" "${OUT}" ${ARGS} RESULT_VARIABLE rc
                OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 77 OR out MATCHES "SKIP:")
  message(STATUS "SKIP: no usable Vulkan device or mode")
  return()
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${TOOL} exited ${rc}:\n${out}\n${err}")
endif()
string(REGEX MATCH "water: [^\n]*" water "${out}")
message(STATUS "${water}")
set(all "${out}\n${err}")
foreach(_x EXPECT EXPECT2)
  if(${_x})
    if(NOT all MATCHES "${${_x}}")
      message(FATAL_ERROR "no \"${${_x}}\" in the output:\n${water}")
    endif()
  endif()
endforeach()
if(NOT PYTHON)
  message(STATUS "${DUMP}: the log checked; no Python for the picture")
  return()
endif()
if(NOT BOX)
  set(BOX "0,0,0,0")
endif()
set(probe [=[
import hashlib, sys
try:
    from PIL import Image
except ImportError:
    print('NOPIL')
    sys.exit(0)
im = Image.open(sys.argv[1]).convert('RGB')
px = im.load()
w, h = im.size
x0, y0, x1, y1 = (int(v) for v in sys.argv[2].split(','))
sx, sy = w / 1920, h / 1080
n = sum(1 for y in range(int(y0 * sy), int(y1 * sy))
        for x in range(int(x0 * sx), int(x1 * sx)) if min(px[x, y]) > 200)
print('BRIGHT %d SHA %s' % (n, hashlib.sha256(im.tobytes()).hexdigest()))
]=])
execute_process(COMMAND "${PYTHON}" -I -c "${probe}" "${OUT}" "${BOX}" RESULT_VARIABLE prc
                OUTPUT_VARIABLE pout ERROR_VARIABLE perr)
if(pout MATCHES "NOPIL")
  message(STATUS "${DUMP}: the log checked; no Pillow for the picture")
  return()
endif()
if(NOT prc EQUAL 0 OR NOT pout MATCHES "BRIGHT ([0-9]+) SHA ([0-9a-f]+)")
  message(FATAL_ERROR "the picture probe failed (${prc}):\n${pout}\n${perr}")
endif()
set(bright "${CMAKE_MATCH_1}")
set(sha "${CMAKE_MATCH_2}")
message(STATUS "${OUT}: ${bright} bright pixels in ${BOX}, RGB SHA-256 ${sha}")
if(DEFINED MAXBRIGHT AND bright GREATER MAXBRIGHT)
  message(FATAL_ERROR "${bright} bright pixels in the box ${BOX}, more than ${MAXBRIGHT}")
endif()
if(SHA)
  if(NOT err MATCHES "llvmpipe")
    message(STATUS "the picture's SHA-256 is lavapipe's: not compared on this device")
  elseif(NOT sha STREQUAL SHA)
    message(FATAL_ERROR "the picture's SHA-256 ${sha}, not ${SHA}")
  endif()
endif()
message(STATUS "${DUMP}: the log and the picture checked")
