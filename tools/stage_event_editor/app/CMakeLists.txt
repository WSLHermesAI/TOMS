# tools/stage_event_editor/app -- stage_event_editor: the stage and event editors as one Windows app (Qt 6 Widgets), the
# stage map drawn by the game's renderer (studio_bgfx's GameCanvasView). Added by ../CMakeLists.txt when Qt is there.
if(NOT TARGET studio_common)
    add_subdirectory("${CMAKE_SOURCE_DIR}/tools/studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()
if(NOT TARGET studio_bgfx OR NOT TARGET toms_anim)
    message(STATUS "[toms] stage_event_editor skipped: needs studio_bgfx (the game renderer in Qt) and toms_anim")
    return()
endif()

add_executable(stage_event_editor WIN32
    Main.cpp
    AppShell.h      AppShell.cpp
    EventEditor.h   EventEditor.cpp
    EventForms.cpp  EventTabs.cpp   EventNpcs.cpp
    EventWidgets.h  EventWidgets.cpp
    StageEditor.h   StageEditor.cpp
    StageCanvas.h   StageCanvas.cpp
    ObjectWindow.h  ObjectWindow.cpp
    SpriteBank.h    SpriteBank.cpp
    StageDraw.h     StageDraw.cpp
    StageText.h     StageText.cpp
    FlowLayout.h    FlowLayout.cpp)
set_target_properties(stage_event_editor PROPERTIES AUTOMOC ON FOLDER "tools/stage_event_editor"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
toms_target_defaults(stage_event_editor)
# Qt's `emit` would clash with the engine's Logger::emit in files that see both: these files use Q_EMIT.
target_compile_definitions(stage_event_editor PRIVATE QT_NO_EMIT)
find_package(Qt6 COMPONENTS Network QUIET)   # the Event page checks for the HTML editors' local server
target_link_libraries(stage_event_editor PRIVATE Qt6::Widgets studio_common studio_bgfx toms_anim stage_core)
if(TARGET Qt6::Network)
    target_link_libraries(stage_event_editor PRIVATE Qt6::Network)
else()
    target_compile_definitions(stage_event_editor PRIVATE TOMS_NO_QT_NETWORK)
endif()

# Copy the Qt DLLs and plugins next to the exe, so double-click and F5 both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_se_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_se_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_se_windeployqt "${_se_windeployqt_exe}")
    endif()
    if(_se_windeployqt)
        add_custom_command(TARGET stage_event_editor POST_BUILD
            COMMAND "${_se_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:stage_event_editor>"
            COMMENT "windeployqt: copying Qt runtime next to stage_event_editor.exe"
            VERBATIM)
    endif()
    # windeployqt copies only the windows platform plugin; --selftest runs with -platform offscreen.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET stage_event_editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:stage_event_editor>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:stage_event_editor>/platforms/")
    endif()
endif()

if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    # The window offscreen on the real floors: paint / undo, ✎ Edit with a linked event, a type's details, auto-place,
    # the checks and story tables, then tabs <-> separate windows (nothing is written).
    add_test(NAME stage_editor.app_selftest
             COMMAND stage_event_editor -platform offscreen --selftest "--root=${CMAKE_SOURCE_DIR}")
    set_tests_properties(stage_editor.app_selftest PROPERTIES LABELS "unit;stage_editor" TIMEOUT 120)
endif()
