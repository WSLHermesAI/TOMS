# tools/font/qt -- font_editor: the font tool's window (docs/engine/09_FONT_TOOL.md).
# Added by tools/font/CMakeLists.txt when Qt6::Widgets is there. Uses tools/studio_common's theme.
if(NOT TARGET studio_common)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../studio_common" "${CMAKE_BINARY_DIR}/tools/studio_common")
endif()

add_executable(font_editor WIN32 Main.cpp FontWindow.h FontWindow.cpp PageView.h PageView.cpp)
set_target_properties(font_editor PROPERTIES AUTOMOC ON FOLDER "tools/font"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
toms_target_defaults(font_editor)
target_link_libraries(font_editor PRIVATE Qt6::Widgets studio_common font_cli atlas_cli)

# Copy the Qt DLLs and plugins next to the exe, so double-click and F5 both work.
if(WIN32)
    if(TARGET Qt6::windeployqt)
        set(_font_windeployqt $<TARGET_FILE:Qt6::windeployqt>)
    else()
        find_program(_font_windeployqt_exe windeployqt HINTS "${Qt6_DIR}/../../../bin")
        set(_font_windeployqt "${_font_windeployqt_exe}")
    endif()
    if(_font_windeployqt)
        add_custom_command(TARGET font_editor POST_BUILD
            COMMAND "${_font_windeployqt}" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:font_editor>"
            COMMENT "windeployqt: copying Qt runtime next to font_editor.exe"
            VERBATIM)
    endif()
    # windeployqt copies only the windows platform plugin; --selftest runs with -platform offscreen.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET font_editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:font_editor>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                    "$<TARGET_FILE_DIR:font_editor>/platforms/")
    endif()
endif()

if(DEFINED TOMS_BUILD_TESTS AND TOMS_BUILD_TESTS)
    # The window offscreen: add the pictures of tests/font/connect_score_pictures, edit characters, test texts,
    # save, open the saved font, save again.
    add_test(NAME font.editor_selftest
             COMMAND font_editor -platform offscreen --selftest "${CMAKE_SOURCE_DIR}/tests/font/connect_score_pictures"
                     "${CMAKE_BINARY_DIR}/test-run/font_editor_selftest")
    set_tests_properties(font.editor_selftest PROPERTIES LABELS "unit;font" TIMEOUT 60)
endif()
