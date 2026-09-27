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
target_include_directories(toms_editor PRIVATE src ${STAGE_EDITOR_DIR} ${TOMS_THIRD_PARTY_DIR}/json)
target_link_libraries(toms_editor PRIVATE Qt6::Widgets toms_game_host)
set_target_properties(toms_editor PROPERTIES
    FOLDER "toms"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Copy the Qt DLLs and plugins next to the exe, so F5 and double-click both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_windeployqt "${_windeployqt_exe}")
    endif()
    if(_windeployqt)
        add_custom_command(TARGET toms_editor POST_BUILD
            COMMAND "${_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:toms_editor>"
            COMMENT "windeployqt: copying Qt runtime next to toms_editor.exe"
            VERBATIM)
    else()
        message(WARNING "[toms] windeployqt not found: copy the Qt DLLs next to toms_editor.exe or add <Qt>/bin to PATH")
    endif()
endif()
