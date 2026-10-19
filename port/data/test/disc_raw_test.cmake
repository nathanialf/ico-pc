# port/data/test/disc_raw_test.cmake: the vfs_disc_jpn_bin, _cue and _chd
# tests (port/data/CMakeLists.txt). Makes a raw MODE2/2352 .bin and its
# .cue from the disc's .iso in DIR (make_raw_cd.py; once, kept for the next
# run), for KIND chd also a CD .chd from the .cue (chdman createcd; the
# test is skipped when chdman is not on PATH), then runs
# `vfs_test same ISO <the image> SLUG`. Prints SKIPPED (the test's
# SKIP_REGULAR_EXPRESSION) when the .iso or chdman is absent.
#
#   cmake -DPYTHON=... -DHELPER=.../make_raw_cd.py -DVFS_TEST=... -DISO=...
#         -DDIR=... -DNAME=Ico_JPN -DSLUG=jpn -DKIND=bin|cue|chd -P disc_raw_test.cmake
if(NOT EXISTS "${ISO}")
    message("SKIPPED: ${ISO} is absent")
    return()
endif()
if(KIND STREQUAL "chd")
    find_program(_chdman chdman)
    if(NOT _chdman)
        message("SKIPPED: chdman is not on PATH")
        return()
    endif()
endif()
execute_process(COMMAND "${PYTHON}" -I "${HELPER}" "${ISO}" "${DIR}" "${NAME}"
                RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
    message(FATAL_ERROR "make_raw_cd.py failed (${_r})")
endif()
set(_image "${DIR}/${NAME}.${KIND}")
if(KIND STREQUAL "chd" AND (NOT EXISTS "${_image}" OR "${DIR}/${NAME}.bin" IS_NEWER_THAN "${_image}"))
    file(REMOVE "${_image}")
    execute_process(COMMAND "${_chdman}" createcd -i "${DIR}/${NAME}.cue" -o "${_image}"
                    RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        file(REMOVE "${_image}")
        message(FATAL_ERROR "chdman createcd failed (${_r})")
    endif()
endif()
execute_process(COMMAND "${VFS_TEST}" same "${ISO}" "${_image}" "${SLUG}" RESULT_VARIABLE _r)
if(_r EQUAL 77)
    message("SKIPPED: vfs_test same skipped")
elseif(NOT _r EQUAL 0)
    message(FATAL_ERROR "vfs_test same ${_image}: failed (${_r})")
endif()
