# run_scenario.cmake -- one scenario test (run by CTest; see tests/CMakeLists.txt and docs/gameplay/08_TEST_SCENARIOS.md).
#
#   cmake -DGAME=<toms_game> -DNAME=<scenario> -DSCENARIO=<its folder> -DWORK=<scratch dir> -P run_scenario.cmake
#
# Runs the game in a fresh folder (its own save folder, so the player's saves are never read or written), straight in
# the scenario's scene, for the scenario's "test" frames and keys. The game exits 3 when the scene it expects is not
# showing. The last frame is kept as <WORK>/<NAME>.png to look at.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/save")
set(ENV{TOMS_SAVE_DIR} "${WORK}/save")
execute_process(COMMAND "${GAME}" "--scenario=${SCENARIO}" --scenario-test --fixed-dt=16 --design=1024x768 "--screenshot=${WORK}/${NAME}.png"
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 90)
string(REGEX MATCHALL "\\[scenario\\][^\n]*(\n  [^\n]*)*" _lines "${_out}")
string(JOIN "\n" _report ${_lines})
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "scenario ${NAME}: toms_game exited with ${_rc}\n${_report}\n---- full output ----\n${_out}")
endif()
message(STATUS "${_report}\n  screenshot: ${WORK}/${NAME}.png")
