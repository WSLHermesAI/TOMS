# editor/ -- toms_editor: Qt6 Widgets + the bgfx game viewport + the old stage editor.
# Only configured when Qt 6 (MSVC 64-bit kit) was found (cmake/TomsPrerequisites.cmake).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(STAGE_EDITOR_DIR ${TOMS_STAGE_EDITOR})

add_executable(toms_editor
    src/main.cpp
    src/editor_window.h src/editor_window.cpp
    src/bgfx_viewport.h src/bgfx_viewport.cpp
    # Windows Debug: memory still allocated at exit (the game's leak check, docs/engine/15_LEAK_CHECK.md)
    ${CMAKE_SOURCE_DIR}/src/game/src/leak_check.h ${CMAKE_SOURCE_DIR}/src/game/src/leak_check.cpp
    # The existing stage editor, compiled as it is (only its main.cpp is left out).
    ${STAGE_EDITOR_DIR}/mainwindow.h ${STAGE_EDITOR_DIR}/mainwindow.cpp
    ${STAGE_EDITOR_DIR}/stagescene.h ${STAGE_EDITOR_DIR}/stagescene.cpp
    ${STAGE_EDITOR_DIR}/stagemodel.h ${STAGE_EDITOR_DIR}/stagemodel.cpp
    ${STAGE_EDITOR_DIR}/catalog.h)
toms_target_defaults(toms_editor)
# Qt #defines `emit`, which breaks the core Logger::emit() (src/engine/log.h) in files that see
# both Qt and the game headers. Those files use Q_EMIT; the old stage editor keeps `emit`.
set_source_files_properties(src/main.cpp src/editor_window.cpp src/bgfx_viewport.cpp
    PROPERTIES COMPILE_DEFINITIONS QT_NO_EMIT)
target_include_directories(toms_editor PRIVATE src ${STAGE_EDITOR_DIR} ${TOMS_THIRD_PARTY_DIR}/json
                           ${CMAKE_SOURCE_DIR}/src/game/src)   # leak_check.h
# studio_common: the editors' shared Qt code -- here the English / 中文 switch (Language.h).
if(NOT TARGET studio_common)
    add_subdirectory("${CMAKE_SOURCE_DIR}/tools/studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()
target_link_libraries(toms_editor PRIVATE Qt6::Widgets toms_game_host studio_common)
set_target_properties(toms_editor PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ui_editor: the game's UI files (.rml / .rcss) beside the running game, every edit shown as it is typed
# (docs/gameplay/01_RMLUI.md "The UI editor"). It shares toms_editor's game viewport.
add_executable(ui_editor
    src/uied_main.cpp
    src/uied_window.h src/uied_window.cpp
    src/uied_logic.h src/uied_logic.cpp
    src/uied_code_edit.h src/uied_code_edit.cpp
    src/bgfx_viewport.h src/bgfx_viewport.cpp)
toms_target_defaults(ui_editor)
set_source_files_properties(src/uied_main.cpp src/uied_window.cpp
    TARGET_DIRECTORY ui_editor PROPERTIES COMPILE_DEFINITIONS QT_NO_EMIT)
target_include_directories(ui_editor PRIVATE src ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ui_editor PRIVATE Qt6::Widgets toms_game_host studio_common)
set_target_properties(ui_editor PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# battle_editor: tune the battle beside the running game (docs/gameplay/13_BATTLE_EDITOR.md). Its own program (the
# owner 2026-10-25: not more put into toms_editor); it shares the game viewport like ui_editor.
add_executable(battle_editor
    src/bed_main.cpp
    src/bed_window.h src/bed_window.cpp
    src/bed_logic.h src/bed_logic.cpp
    src/bgfx_viewport.h src/bgfx_viewport.cpp)
toms_target_defaults(battle_editor)
set_source_files_properties(src/bed_main.cpp src/bed_window.cpp
    TARGET_DIRECTORY battle_editor PROPERTIES COMPILE_DEFINITIONS QT_NO_EMIT)
target_include_directories(battle_editor PRIVATE src ${CMAKE_SOURCE_DIR}/src/engine/src ${TOMS_THIRD_PARTY_DIR}/json)
target_link_libraries(battle_editor PRIVATE Qt6::Widgets toms_game_host studio_common)
set_target_properties(battle_editor PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(TOMS_BUILD_TESTS)
    # the calculator, the setups, the files written back, the window's lists -- no GPU
    add_test(NAME battle_editor.selftest COMMAND battle_editor -platform offscreen --selftest)
    set_tests_properties(battle_editor.selftest PROPERTIES LABELS "unit;studio" TIMEOUT 60)
    # with a GPU: three monsters, ▶ Battle, the log, Win now
    add_test(NAME smoke.battle_editor COMMAND battle_editor "--smoke=${CMAKE_BINARY_DIR}/test-run/battle_editor_smoke.png")
    set_tests_properties(smoke.battle_editor PROPERTIES LABELS "smoke" TIMEOUT 120)
endif()

if(TOMS_BUILD_TESTS)
    # the text logic and file handling, no GPU
    add_test(NAME ui_editor.selftest COMMAND ui_editor -platform offscreen --selftest)
    set_tests_properties(ui_editor.selftest PROPERTIES LABELS "unit;studio" TIMEOUT 60)
    # with a GPU: typed text shows in the game before saving, the file stays, inspecting finds the element
    add_test(NAME smoke.ui_editor COMMAND ui_editor "--smoke=${CMAKE_BINARY_DIR}/test-run/ui_editor_smoke.png")
    set_tests_properties(smoke.ui_editor PROPERTIES LABELS "smoke" TIMEOUT 120)
endif()

# Copy the Qt DLLs and plugins next to the exe, so F5 and double-click both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_windeployqt "${_windeployqt_exe}")
    endif()
    if(_windeployqt)
        # each editor copies them itself, so building only one of them (F5 on ui_editor or battle_editor after
        # Delete Cache) still gives a program that starts
        foreach(_qt_editor toms_editor ui_editor battle_editor)
            add_custom_command(TARGET ${_qt_editor} POST_BUILD
                COMMAND "${_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                        --no-opengl-sw "$<TARGET_FILE:${_qt_editor}>"
                COMMENT "windeployqt: copying Qt runtime next to ${_qt_editor}.exe"
                VERBATIM)
        endforeach()
        # windeployqt copies only the windows platform plugin; the --selftest runs use -platform offscreen.
        if(TARGET Qt6::QOffscreenIntegrationPlugin)
            foreach(_qt_editor ui_editor battle_editor)
                add_custom_command(TARGET ${_qt_editor} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_qt_editor}>/platforms"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                            "$<TARGET_FILE_DIR:${_qt_editor}>/platforms/")
            endforeach()
        endif()
    else()
        message(WARNING "[toms] windeployqt not found: copy the Qt DLLs next to the editors or add <Qt>/bin to PATH")
    endif()
endif()
