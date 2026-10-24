# tools/stage_event_editor -- the stage and event editors as one Windows app, on the game's stack (Qt 6 + C++ + bgfx).
# docs/gameplay/02_STAGE_EDITOR.md section 6; the HTML editors in tools/web_editors stay, on the same files.
#
#   stage_core          the editors' rules without Qt (core/): floors, cells, undo, checks, story check,
#                       auto-place, save, the floor index; the event editor's state, validator and save
#   stage_core_test     its tests on the real floors (unit.stage_core_test)
#   stage_event_editor  the window (Qt 6 Widgets): the stage editor drawn by the game's renderer (GameCanvasView)
#                       and the event editor; tabs or two windows (View menu)
#
# Added from the root CMakeLists.txt (desktop builds).
add_library(stage_core STATIC
    core/stage_project.h core/stage_project.cpp
    core/stage_checks.h  core/stage_checks.cpp
    core/stage_save.h    core/stage_save.cpp
    core/event_project.h core/event_project.cpp)
toms_target_defaults(stage_core)
target_include_directories(stage_core PUBLIC core "${TOMS_THIRD_PARTY_DIR}/json")
set_target_properties(stage_core PROPERTIES FOLDER "tools/stage_event_editor")

if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    add_executable(stage_core_test tests/stage_core_test.cpp)
    toms_target_defaults(stage_core_test)
    target_link_libraries(stage_core_test PRIVATE stage_core)
    set_target_properties(stage_core_test PROPERTIES FOLDER "tools/stage_event_editor")
    add_test(NAME unit.stage_core_test COMMAND stage_core_test "${CMAKE_SOURCE_DIR}")
    set_tests_properties(unit.stage_core_test PROPERTIES LABELS "unit;stage_editor" TIMEOUT 120)
endif()

if(TOMS_HAVE_QT AND TARGET Qt6::Widgets AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()
