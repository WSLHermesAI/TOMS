# run_smoke.cmake -- one screenshot smoke test (run by CTest; see tests/CMakeLists.txt).
#
#   cmake -DGAME=<toms_game> -DDIFF=<image_diff> -DNAME=<test> -DGOLDEN=<golden.png>
#         -DWORK=<scratch dir> -DTOLERANCE=<percent> "-DGAME_ARGS=--frames=60|--keys=..." -P run_smoke.cmake
#
# Runs the game in a fresh folder (so no earlier save changes what the title shows), then compares
# its screenshot with the reference image. Set TOMS_UPDATE_GOLDEN=1 to write the screenshot as the
# new reference instead (after an intended change to what the screen looks like).
string(REPLACE "|" ";" GAME_ARGS "${GAME_ARGS}")   # back to a list: one argument each
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(_shot "${WORK}/${NAME}.png")

execute_process(COMMAND "${GAME}" ${GAME_ARGS} "--screenshot=${_shot}"
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 60)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "toms_game exited with ${_rc}:\n${_out}")
endif()
if(NOT EXISTS "${_shot}")
    message(FATAL_ERROR "toms_game wrote no screenshot:\n${_out}")
endif()

# Only the test named after the reference writes it (stage_vulkan compares with stage.png but must
# never replace the Direct3D 11 image).
get_filename_component(_golden_name "${GOLDEN}" NAME_WE)
if("$ENV{TOMS_UPDATE_GOLDEN}" STREQUAL "1" AND _golden_name STREQUAL NAME)
    get_filename_component(_dir "${GOLDEN}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(COPY_FILE "${_shot}" "${GOLDEN}")
    message(STATUS "reference image updated: ${GOLDEN}")
    return()
endif()
if(NOT EXISTS "${GOLDEN}")
    message(FATAL_ERROR "no reference image ${GOLDEN} yet; create it with TOMS_UPDATE_GOLDEN=1 (see docs/09_TESTS.md)")
endif()

execute_process(COMMAND "${DIFF}" "${_shot}" "${GOLDEN}" "${TOLERANCE}" "${WORK}/${NAME}.diff.png"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _dout ERROR_VARIABLE _dout)
message(STATUS "${_dout}")
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${NAME}: the screen differs from ${GOLDEN}\n  screenshot: ${_shot}\n  diff:       ${WORK}/${NAME}.diff.png")
endif()
