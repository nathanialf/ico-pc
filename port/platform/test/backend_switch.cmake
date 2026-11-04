# port/platform/test/backend_switch.cmake: the backend_switch_window test,
# run as
#   cmake -DICO_PC=<window-build ico_pc> -DISO=<disc image> -DDIR=<work folder>
#         [-DLVP=<lavapipe ICD json>] -P backend_switch.cmake
# The window build of the program is copied into an empty folder (portable:
# its ico-pc.ini, config, logs and card folder beside it) and started on
# Vulkan (lavapipe) under SDL's offscreen video driver; [dev]
# backend_switch_test = "opengl,vulkan" drives Options > Graphics > Apply's
# own path (window_switch.c selftestPoll): Vulkan -> OpenGL (Mesa's llvmpipe:
# LIBGL_ALWAYS_SOFTWARE=1) -> Vulkan, each through ico_window_switch_backend
# and ico_window_reopen as the game's menu runs them, then Keep. The log must
# show both switches applied and kept, the test's "done, 2 switches, 0
# failed", OpenGL's compile thread line, every switch step up to "up in", no
# WATCHDOG or CRASH block, and the run to its last tick with exit 0. Without
# the disc image the test is skipped.

if(NOT EXISTS "${ISO}")
    message("backend_switch_window: skipped (no disc image at ${ISO})")
    return()
endif()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
file(COPY "${ICO_PC}" DESTINATION "${DIR}")
get_filename_component(_exe "${ICO_PC}" NAME)
file(WRITE "${DIR}/ico-pc.ini"
     "iso=${ISO}\nverify=0\nuse_iso=1\naudio=0\nticks=1500\nwatchdog=120\ntrace=0\n"
     "portable=1\ninput_record=0\nbackend=vulkan\nbackend_switch_test=opengl,vulkan\n")
set(_env "LIBGL_ALWAYS_SOFTWARE=1" "SDL_VIDEO_DRIVER=offscreen")
if(LVP AND EXISTS "${LVP}")
    list(APPEND _env "VK_ICD_FILENAMES=${LVP}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${_env} "${DIR}/${_exe}"
                WORKING_DIRECTORY "${DIR}" TIMEOUT 1450
                RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
set(_log "${DIR}/logs/ico-pc.log")
if(NOT EXISTS "${_log}")
    message(FATAL_ERROR "backend_switch_window: no log (exit ${_rc})")
endif()
file(READ "${_log}" _text)

set(_bad "")
foreach(_line
        "window: backend switch test: vulkan -> opengl: applied and kept in"
        "window: backend switch test: opengl -> vulkan: applied and kept in"
        "window: backend switch test: done, 2 switches, 0 failed"
        "rhi_gl: shaders: compiled on a thread of their own"
        "window: switch: vulkan -> opengl begins"
        "window: switch to opengl: starting the renderer (the device and its shaders)"
        "window: switch to opengl: up in "
        "window: switch to vulkan: up in "
        "ico_pc: diagnostics: watchdog: switching the renderer from vulkan to opengl"
        "ico_pc: exit: ticks= reached")
    string(FIND "${_text}" "${_line}" _at)
    if(_at LESS 0)
        string(APPEND _bad "\n  no \"${_line}\" line")
    endif()
endforeach()
foreach(_line "ico_pc: WATCHDOG" "ico_pc: CRASH" "FAILED (" "OpenGL does not start")
    string(FIND "${_text}" "${_line}" _at)
    if(NOT _at LESS 0)
        string(REGEX MATCH "[^\n]*${_line}[^\n]*" _hit "${_text}")
        string(APPEND _bad "\n  ${_hit}")
    endif()
endforeach()
if(NOT _rc EQUAL 0)
    string(APPEND _bad "\n  exit ${_rc}")
endif()
if(_bad)
    message(FATAL_ERROR "backend_switch_window: ${_bad}\n(log: ${_log})")
endif()
string(REGEX MATCHALL "window: backend switch test: [a-z]+ -> [a-z]+: applied and kept in [0-9.]+ s"
       _times "${_text}")
message("backend_switch_window: ${_times}")
